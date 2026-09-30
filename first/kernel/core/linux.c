// linux.c - Linux 二进制兼容层 (Linuxulator) 骨架
//
// 目标: 在 FSOS 上加载并运行 Linux x86-64 用户态 ELF 控制台程序 (子集)。
// 设计要点:
//   * 仅支持 PIE (ET_DYN) —— 自选高地址基址 LINUX_BASE, 避免与恒等映射的内核区
//     (<0x1D000000) 冲突; 处理 R_X86_64_RELATIVE 重定位。
//   * 每个 Linux 进程拥有独立页表: 克隆内核页表 (保留内核映射, 供 syscall 返回用)
//     + 把 ELF 段映射到独立物理帧 (pmm)。
//   * 构造标准 Linux 初始栈 (argc/argv/envp/auxv) 后 iretq 进 ring3。
//   * 同一 `syscall` 指令被 Linux libc 使用; 由 syscall.c 按 is_linux 标志转发至此。
#include "linux.h"
#include "paging.h"
#include "sched.h"
#include "filesys.h"
#include "kb.h"
#include "kheap.h"
#include "io.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>
#include "hello_elf.inc"   // 内嵌 HELLO.ELF (Linuxulator 演示程序, 见 tools/gen_linux_hello.py)

// ---------------- 小工具 ----------------
static void lm_memset(void* d, int v, uint64_t n) {
    uint8_t* p = (uint8_t*)d;
    for (uint64_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void lm_memcpy(void* d, const void* s, uint64_t n) {
    uint8_t* p = (uint8_t*)d; const uint8_t* q = (const uint8_t*)s;
    for (uint64_t i = 0; i < n; i++) p[i] = q[i];
}
static void lser_putc(char c) {
    for (unsigned i = 0; i < 0x2000; ++i) { if (inb(0x3FD) & 0x20) { outb(0x3F8, (uint8_t)c); return; } }
}
static void lser_puts(const char* s) { for (; *s; ++s) lser_putc(*s); }
static int lm_strlen(const char* s) { int n = 0; while (s[n]) n++; return n; }

// 启动期把内嵌的 HELLO.ELF 写入 FS (若不存在), 供 Ctrl+Alt+L / 自检加载。
void linux_ensure_hello(void) {
    if (fs_size("HELLO.ELF") >= 0) return;   // 已存在
    fs_write_bin("HELLO.ELF", (const char*)g_hello_elf, (int)g_hello_elf_len);
}

#ifndef LINUX_SELFTEST
#define LINUX_SELFTEST 1   // 打开后每次启动自动运行 HELLO.ELF 并打串口, 用于验证 Linuxulator; 不需要时改回 0
#endif
// 可选自检: 自动加载并运行 HELLO.ELF, 结果经 lser_putc 打串口。
// 默认关闭; 构建期定义 LINUX_SELFTEST=1 可在无显示环境验证 Linuxulator。
void linux_selftest(void) {
    if (!LINUX_SELFTEST) return;
    linux_ensure_hello();
    int pid = linux_exec("HELLO.ELF");
    lser_puts("[linux] selftest: linux_exec(HELLO.ELF) -> pid ");
    char tmp[12]; int ti = 0, v = pid;
    if (v < 0) { tmp[ti++] = '-'; v = -v; }
    if (v == 0) tmp[ti++] = '0';
    while (v > 0) { tmp[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti) lser_putc(tmp[--ti]);
    lser_puts("\r\n");
}

// ---------------- ELF 结构 (Linux x86-64) ----------------
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) LEhdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) LPhdr;

typedef struct {
    uint64_t d_tag;
    uint64_t d_val;
} __attribute__((packed)) LDyn;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) LRela;

#define L_PT_LOAD      1
#define L_PT_DYNAMIC   2
#define L_ET_DYN       3
#define L_EM_X86_64    0x3E
#define R_X86_64_RELATIVE 8
#define LREL_TYPE(x)   ((x) & 0xFFFFFFFFULL)

// ---------------- 内存布局常量 ----------------
#define LINUX_BASE       0x28000000ULL   // PIE 加载基址 (用户区, 模块窗口 1GB 之下)
#define LINUX_STACK_TOP  0x3F000000ULL   // 用户栈顶
#define LINUX_STACK_PAGES 4
#define MMAP_BASE        0x30000000ULL   // mmap/brk 虚拟区起点

// ---------------- per-process Linux 上下文 ----------------
#define LINUX_MAX_PROCS 32
static linux_ctx_t g_ctx[LINUX_MAX_PROCS];

static linux_ctx_t* ctx_by_pid(int pid) {
    if (pid < 0 || pid >= LINUX_MAX_PROCS) return 0;
    if (!g_ctx[pid].in_use) return 0;
    return &g_ctx[pid];
}

linux_ctx_t* linux_ctx_current(void) {
    int pid = sched_current_pid();
    return ctx_by_pid(pid);
}

void linux_ctx_free_current(void) {
    int pid = sched_current_pid();
    if (pid < 0 || pid >= LINUX_MAX_PROCS) return;
    g_ctx[pid].in_use = 0;
}

static linux_ctx_t* ctx_alloc(int pid) {
    if (pid < 0 || pid >= LINUX_MAX_PROCS) return 0;
    g_ctx[pid].in_use = 1;
    g_ctx[pid].brk = LINUX_BASE + 0x100000;
    g_ctx[pid].mmap_next = MMAP_BASE;
    return &g_ctx[pid];
}

// ---------------- 加载 ----------------
int linux_exec(const char* path) {
    uint8_t* buf = (uint8_t*)kmalloc(65536);
    if (!buf) return -1;

    int n = fs_read_bin(path, (char*)buf, 65536);
    if (n <= 0) { kfree(buf); return -2; }

    LEhdr* eh = (LEhdr*)buf;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        kfree(buf); return -3;
    }
    if (eh->e_ident[4] != 2) { kfree(buf); return -3; }           // ELFCLASS64
    if (eh->e_machine != L_EM_X86_64) { kfree(buf); return -3; }  // x86-64
    if (eh->e_type != L_ET_DYN) { kfree(buf); return -4; }        // 仅支持 PIE(ET_DYN)

    uint64_t phoff    = eh->e_phoff;
    uint16_t phnum    = eh->e_phnum;
    uint16_t phentsz  = eh->e_phentsize;
    uint64_t entry    = eh->e_entry;
    uint64_t base     = LINUX_BASE;

    uint64_t pml4 = paging_clone_kernel();
    if (!pml4) { kfree(buf); return -5; }

    // 1) 映射所有 PT_LOAD 段到独立物理帧
    LPhdr* ph = (LPhdr*)(buf + phoff);
    for (uint16_t i = 0; i < phnum; i++) {
        if (ph[i].p_type != L_PT_LOAD) continue;
        uint64_t vaddr  = base + ph[i].p_vaddr;
        uint64_t filesz = ph[i].p_filesz;
        uint64_t memsz  = ph[i].p_memsz;
        uint64_t npages = (vaddr & 0xFFF) + memsz;
        npages = (npages + 0xFFF) >> 12;
        for (uint64_t pg = 0; pg < npages; pg++) {
            uint64_t v = (vaddr & ~0xFFFULL) + pg * 0x1000;
            uint64_t pa = pmm_alloc_frame();
            if (!pa) { kfree(buf); return -6; }
            paging_map_4k(v, pa, PG_USER | PG_RW);
            uint8_t* dst = (uint8_t*)(uintptr_t)pa;   // 恒等映射: pa 即虚拟地址
            lm_memset(dst, 0, 0x1000);
            uint64_t foff    = ph[i].p_offset + pg * 0x1000;
            uint64_t rem     = (pg * 0x1000 < filesz) ? (filesz - pg * 0x1000) : 0;
            uint64_t cpy     = (rem < 0x1000) ? rem : 0x1000;
            if (cpy > 0) lm_memcpy(dst, buf + foff, cpy);
        }
    }

    // 切到新页表, 以便按虚拟地址写重定位与初始栈
    uint64_t saved_cr3 = paging_get_cr3();
    paging_switch(pml4);

    // 2) 处理 .rela.dyn 中的 R_X86_64_RELATIVE 重定位 (静态 PIE 必需)
    for (uint16_t i = 0; i < phnum; i++) {
        if (ph[i].p_type != L_PT_DYNAMIC) continue;
        LDyn* dyn = (LDyn*)(uintptr_t)(base + ph[i].p_vaddr);
        uint64_t rela_off = 0, rela_sz = 0, relaent = 24;
        for (;;) {
            if (dyn->d_tag == 0) break;                 // DT_NULL
            if (dyn->d_tag == 7)  rela_off = dyn->d_val; // DT_RELA
            if (dyn->d_tag == 8)  rela_sz  = dyn->d_val; // DT_RELASZ
            if (dyn->d_tag == 9)  relaent  = dyn->d_val; // DT_RELAENT
            dyn++;
        }
        if (rela_off && rela_sz) {
            LRela* r = (LRela*)(uintptr_t)(base + rela_off);
            int nrel = (int)(rela_sz / (relaent ? relaent : 24));
            for (int k = 0; k < nrel; k++) {
                if (LREL_TYPE(r[k].r_info) == R_X86_64_RELATIVE) {
                    uint64_t* loc = (uint64_t*)(uintptr_t)(base + r[k].r_offset);
                    *loc = base + (uint64_t)r[k].r_addend;
                }
            }
        }
        break;
    }

    // 3) 构造用户栈: 字符串 + argc/argv/envp/auxv
    uint64_t top = LINUX_STACK_TOP;
    for (int i = 0; i < LINUX_STACK_PAGES; i++)
        paging_map_4k(top - (i + 1) * 0x1000, pmm_alloc_frame(), PG_USER | PG_RW);

    uint64_t rand_addr = top - 16;
    uint64_t argv0     = top - 32;
    uint8_t* rp = (uint8_t*)(uintptr_t)rand_addr;
    for (int i = 0; i < 16; i++) rp[i] = (uint8_t)(0x30 + i);   // 伪随机
    const char* av0 = "linux";
    uint8_t* ap = (uint8_t*)(uintptr_t)argv0;
    for (int i = 0; av0[i]; i++) ap[i] = (uint8_t)av0[i];
    ap[5] = 0;

    uint64_t user_rsp = (top - 512) & ~0xFULL;
    if ((user_rsp & 0xF) == 0) user_rsp += 8;   // 满足 ABI: rsp%16==8

    uint64_t words[24]; int wi = 0;
    words[wi++] = 1;                 // argc
    words[wi++] = argv0;             // argv[0]
    words[wi++] = 0;                 // argv NULL
    words[wi++] = 0;                 // envp NULL
    words[wi++] = 6;  words[wi++] = 0x1000;          // AT_PAGESZ
    words[wi++] = 3;  words[wi++] = base + phoff;    // AT_PHDR
    words[wi++] = 4;  words[wi++] = phentsz;         // AT_PHENT
    words[wi++] = 5;  words[wi++] = phnum;           // AT_PHNUM
    words[wi++] = 9;  words[wi++] = base + entry;    // AT_ENTRY
    words[wi++] = 7;  words[wi++] = 0;               // AT_BASE (无解释器)
    words[wi++] = 25; words[wi++] = rand_addr;       // AT_RANDOM
    words[wi++] = 0;  words[wi++] = 0;               // AT_NULL
    uint64_t* wp = (uint64_t*)(uintptr_t)user_rsp;
    for (int i = 0; i < wi; i++) wp[i] = words[i];

    // 恢复内核页表后再创建进程 (内核线程继续用原 CR3)
    paging_switch(saved_cr3);

    int pid = sched_create_user_process_ex(pml4, base + entry, user_rsp, "linux", 1);
    if (pid > 0) ctx_alloc(pid);   // 登记 per-process 上下文
    kfree(buf);
    return pid;
}

// ---------------- Linux 系统调用子集 ----------------
// 常见号: read=0 write=1 open=2 close=3 stat=4 fstat=5 lseek=8 mmap=9
//         brk=12 rt_sigaction=13 rt_sigprocmask=15 getpid=39
//         access=21 arch_prctl=158 set_tid_address=218 exit=60 exit_group=231
//         set_robust_list=273

uint64_t linux_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    switch (num) {
        case 0: {  // read(fd, buf, len): 仅实现 stdin(fd 0) 来自键盘; 其它 fd 暂不支持
            int fd = (int)a1;
            char* p = (char*)(uintptr_t)a2;
            uint64_t len = a3;
            if (fd == 0) {
                uint64_t got = 0;
                for (uint64_t i = 0; i < len; i++) {
                    int c = kb_poll();
                    if (c == 0) break;                 // 当前无可用按键
                    if (c >= 0x100) break;             // 跳过扩展键 (方向键等)
                    if (c == '\r') c = '\n';           // 回车归一化
                    p[i] = (char)c; got++;
                }
                return got;
            }
            return (uint64_t)-1;   // -EBADF (尚未实现 open())
        }
        case 1: {  // write(fd, buf, len)
            int fd = (int)a1;
            const char* p = (const char*)(uintptr_t)a2;
            uint64_t len = a3;
            if (fd == 1 || fd == 2) {
                for (uint64_t i = 0; i < len; i++) lser_putc(p[i]);
                return len;
            }
            return (uint64_t)-1;   // -EBADF
        }
        case 3:  // close
            return 0;
        case 5: {  // fstat: 填最小 stat, 让 stdout 检查通过
            uint8_t* st = (uint8_t*)(uintptr_t)a2;
            lm_memset(st, 0, 128);
            // st_mode 在偏移 16 (64 位布局), S_IFCHR=020000
            *(uint32_t*)(st + 16) = 0x2000;
            return 0;
        }
        case 12: { // brk (per-process)
            linux_ctx_t* ctx = linux_ctx_current();
            if (!ctx) return 0;
            uint64_t addr = a1;
            if (addr == 0) return ctx->brk;
            if (addr > ctx->brk) {
                uint64_t s = (ctx->brk + 0xFFF) & ~0xFFFULL;
                uint64_t e = (addr + 0xFFF) & ~0xFFFULL;
                for (; s < e; s += 0x1000)
                    paging_map_4k(s, pmm_alloc_frame(), PG_USER | PG_RW);
                ctx->brk = addr;
            }
            return ctx->brk;
        }
        case 9: {  // mmap(addr, len, prot, flags, fd, off) (per-process)
            linux_ctx_t* ctx = linux_ctx_current();
            if (!ctx) return (uint64_t)-1;
            uint64_t len = (a2 + 0xFFF) & ~0xFFFULL;
            uint64_t addr = ctx->mmap_next;
            for (uint64_t s = addr; s < addr + len; s += 0x1000)
                paging_map_4k(s, pmm_alloc_frame(), PG_USER | PG_RW);
            ctx->mmap_next += len;
            return addr;
        }
        case 13: // rt_sigaction
        case 15: // rt_sigprocmask
        case 21: // access
        case 63: // uname
            if (num == 63) {
                uint8_t* u = (uint8_t*)(uintptr_t)a1;
                const char* fields[6] = {"FSOS","fsos","1.0","#linux","x86_64","-"};
                int off = 0;
                for (int f = 0; f < 6; f++) {
                    for (int i = 0; fields[f][i]; i++) u[off + i] = (uint8_t)fields[f][i];
                    u[off + lm_strlen(fields[f])] = 0;
                    off += 65;   // struct utsname 每字段 65 字节
                }
            }
            return 0;
        case 39:  // getpid
        case 186: // gettid
            return (uint64_t)sched_current_pid();
        case 158: // arch_prctl (忽略 FS base 设置)
            return 0;
        case 218: // set_tid_address
            return (uint64_t)sched_current_pid();
        case 273: // set_robust_list
            return 0;
        case 60:  // exit
        case 231: // exit_group
            linux_ctx_free_current();
            sched_exit();
            return 0;   // 不会到达
        default:
            lser_puts("[linux] unhandled syscall ");
            { char tmp[12]; int ti=0, v=(int)num; if(v<0){tmp[ti++]='-';v=-v;}
              if(v==0)tmp[ti++]='0'; while(v>0){tmp[ti++]='0'+v%10;v/=10;}
              while(ti) lser_putc(tmp[--ti]); }
            lser_puts("\r\n");
            return (uint64_t)-38;   // -ENOSYS
    }
}
