// module.c - 内核侧解释器模块加载器 (按需从磁盘载入, 运行完即释放)
//
// 流程: lang_launch("C/C++"|"Java", src, name)
//         -> mod_load_run: 选 VA/LBA, 清零预留区, ata 读入 blob,
//            以 (mod_entry_t)VA 为入口调用, 结束后清零释放。
// 空闲时预留区为空, 内核不含任何解释器代码/数据。
#include "module.h"
#include "mod_abi.h"
#include "ata.h"
#include "kheap.h"
#include "filesys.h"
#include "io.h"
#include "idt.h"       // get_ticks() (PIT, 1ms/次)
#include <stdint.h>

// ---- 串口镜像 (无头 VMware 验证用) ----
static void s_putc(char c) {
    if (c == '\n') s_putc('\r');
    outb(0x3F8, (uint8_t)c);
}

// ---- 控制台滚动缓冲 (GUI 终端窗口后续渲染) ----
#define CON_SZ 16384
static char g_con[CON_SZ];
static int  g_con_head = 0;     // 下一个写入位置
static int  g_con_len  = 0;     // 有效字节数

void console_emit(char c) {
    s_putc(c);                   // 串口镜像
    g_con[g_con_head] = c;
    g_con_head = (g_con_head + 1) % CON_SZ;
    if (g_con_len < CON_SZ) g_con_len++;
}
void console_clear(void) {
    g_con_head = 0;
    g_con_len = 0;
}

int console_drain(char* dst, int len) {
    if (len > g_con_len) len = g_con_len;
    // 从最旧字节开始拷贝
    int start = (g_con_head - g_con_len + CON_SZ) % CON_SZ;
    for (int i = 0; i < len; i++)
        dst[i] = g_con[(start + i) % CON_SZ];
    return len;
}

// ---- 内核侧 syscall 实现 (供模块经函数指针调用) ----
static void* sc_malloc(size_t n) { return kmalloc(n); }
static void  sc_free(void* p)    { kfree(p); }
static void  sc_putc(char c)     { console_emit(c); }
static void  sc_puts(const char* s) { while (*s) console_emit(*s++); }
static int   sc_file_read(const char* name, char* buf, int cap) {
    return fs_read(name, buf, cap);
}
static int   sc_file_write(const char* name, const char* data) {
    return fs_write(name, data);
}
static int   sc_file_list(char names[][24], int max) {
    return fs_list(names, max);
}
static int mod_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int   sc_file_exists(const char* name) {
    char names[16][24];
    int n = fs_list(names, 16);
    for (int i = 0; i < n; i++)
        if (mod_strcmp(names[i], name) == 0) return 1;
    return 0;
}
static void  sc_log(const char* s) { while (*s) s_putc(*s++); }
static uint64_t sc_tick_ms(void) { return (uint64_t)get_ticks(); } // PIT 毫秒计数

static uint32_t g_exec_deadline = 0;
static int sc_poll(void) {
    if (!g_exec_deadline) return 0;
    if ((int32_t)(get_ticks() - g_exec_deadline) >= 0) return -1;
    return 0;
}

// IEEE 802.3 CRC32 (供模块完整性校验)
static uint32_t mod_crc32(const uint8_t* data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFFu;
}

// 分块读 (ata_read_sectors 的 count 为 uint8_t, 上限 255 扇区)
static int ata_read_chunked(uint32_t lba, uint32_t nsec, void* buf) {
    uint8_t* p = (uint8_t*)buf;
    uint32_t off = 0;
    while (off < nsec) {
        uint8_t n = (uint8_t)((nsec - off) > 255 ? 255 : (nsec - off));
        if (ata_read_sectors(lba + off, n, p + (uintptr_t)off * 512) != 0) return -1;
        off += n;
    }
    return 0;
}

static mod_syscalls_t g_sc = {
    sc_malloc, sc_free, sc_putc, sc_puts,
    sc_file_read, sc_file_write, sc_file_list, sc_file_exists,
    sc_log, sc_tick_ms, sc_poll
};

// ---- 载入并运行 ----
static int do_load(const char* which, uintptr_t va, uint32_t lba,
                   const char* src, const char* proc_name) {
    char* p = (char*)va;

    // 1) 先读首扇区拿到头部
    if (ata_read_sectors(lba, 1, (void*)va) != 0) {
        s_putc('!'); sc_log(which); sc_log(" module read FAIL\r\n");
        return -3;
    }
    mod_header_t* h = (mod_header_t*)va;
    if (h->magic != MOD_MAGIC) {
        sc_log(which); sc_log(" module magic mismatch (not loaded?)\r\n");
        return -4;
    }
    if (h->version != MOD_ABI_VERSION) {
        sc_log(which); sc_log(" module ABI version mismatch\r\n");
        return -5;
    }

    // 2) 按头部 size 计算需载入扇区数 (无 size 则读满 MOD_SECTORS 作为上界)
    uint32_t total = MOD_SECTORS;
    if (h->size != 0) {
        uint32_t s = (h->size + 511) / 512;
        if (s > MOD_SECTORS) s = MOD_SECTORS;
        total = s;
    }

    // 3) 仅清零"实际使用的区域"(GB 级窗口下避免清零整段), 再整块读入 blob
    for (uint64_t i = 0; i < (uint64_t)total * 512; i++) p[i] = 0;
    if (ata_read_chunked(lba, total, (void*)va) != 0) {
        s_putc('!'); sc_log(which); sc_log(" module read FAIL\r\n");
        return -3;
    }

    // 4) 可选 CRC32 校验 ([MOD_HDRSZ, size))
    if (h->crc32 != 0 && h->size > MOD_HDRSZ) {
        uint32_t c = mod_crc32((const uint8_t*)va + MOD_HDRSZ, h->size - MOD_HDRSZ);
        if (c != h->crc32) {
            sc_log(which); sc_log(" module CRC mismatch\r\n");
            return -6;
        }
    }

    // 5) 跳入入口执行
    typedef int (*entry_fn_t)(const void*, const char*, const char*);
    entry_fn_t entry = (entry_fn_t)(uintptr_t)(va + h->entry_off);
    // 解释器是协作式的：模块循环通过 sc->poll() 检查时间预算，
    // 防止一个错误的 while(1) 把整个桌面线程永久锁死。
    g_exec_deadline = get_ticks() + 10000;
    int rc = entry(&g_sc, src, proc_name);
    g_exec_deadline = 0;

    // 6) 运行后释放: 仅清零实际使用区域, 空闲内核零占用
    for (uint64_t i = 0; i < (uint64_t)total * 512; i++) p[i] = 0;
    return rc;
}

int mod_load_run(const char* which, const char* src, const char* proc_name) {
    if (which && which[0] == 'C') // "C/C++"
        return do_load(which, MOD_CINT_VA, MOD_CINT_LBA, src, proc_name);
    if (which && which[0] == 'J') // "Java"
        return do_load(which, MOD_JVM_VA, MOD_JVM_LBA, src, proc_name);
    sc_log("mod_load_run: unknown language\r\n");
    return -1;
}
