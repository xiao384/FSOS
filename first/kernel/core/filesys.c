// filesys.c - FSOS 扁平文件区实现 (支持子目录/文件夹)
//
// 目录项结构 (与 MicroPython 端口原实现逐字节兼容, 仅新增 type 字段):
//   { char name[24]; uint32_t lba; uint16_t nsec; uint8_t used; uint8_t type; }
// (24+4+2+1+1 = 32 字节 -> 64 项恰等于 FS_DIR_SECS*512 = 4 扇区)
// 旧磁盘项 type 字段为 padding 0, 读作 FS_TYPE_FILE, 向上兼容。
//
// 目录是一块 FS_DIR_SECS 扇区的"目录项数组", 根目录在 LBA_FS_DIR, 子目录的
// 目录块分配在数据区内 (同文件共享同一数据池, 全局高水位分配, 可嵌套)。
//   - 文件项: lba 指向数据, 占 FS_FILE_SECS 扇区 (16KB)
//   - 目录项: lba 指向子目录块, 占 FS_DIR_SECS 扇区 (2KB)
#include "filesys.h"
#include "layout.h"
#include "ata.h"
#include <stdint.h>

#define FS_DIR_LBA   LBA_FS_DIR
#define FS_DATA_LBA  LBA_FS_DATA

typedef struct {
    char     name[FS_NAME_SZ];
    uint32_t lba;
    uint16_t nsec;
    uint8_t  used;
    uint8_t  type;    // FS_TYPE_FILE / FS_TYPE_DIR
} fs_entry_t;

// 三块缓冲互不重叠: g_dir=根缓存, g_tbuf=正在编辑的目录, g_scan=分配扫描用
static uint8_t g_dir[FS_DIR_SECS * 512];
static int     g_dir_loaded = 0;
static uint8_t g_tbuf[FS_DIR_SECS * 512];
static uint8_t g_scan[FS_DIR_SECS * 512];
static char g_io[FS_MAX_SIZE];        // 单文件 I/O 缓冲 (16KB, 静态避免压栈)

// ---- 最小字符串/内存助手 (内核 -ffreestanding, 不依赖 libc 声明) ----
static int fs_strlen(const char* s) { int n = 0; while (s && s[n]) n++; return n; }
static int fs_strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void fs_ncpy(char* d, const char* s, int n) {   // 拷贝并补 NUL
    int i = 0;
    for (; i < n - 1 && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
}
static void fs_zero(void* p, int n) {
    unsigned char* q = (unsigned char*)p; while (n--) *q++ = 0;
}
static void fs_mmove(void* d, const void* s, int n) {   // 支持重叠的字节搬移
    unsigned char* dd = (unsigned char*)d;
    const unsigned char* ss = (const unsigned char*)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
}

// 单个目录项占用的扇区跨度 (文件/目录不同)
static int span_of(const fs_entry_t* e) {
    return (e->type == FS_TYPE_DIR) ? FS_DIR_SECS : FS_FILE_SECS;
}

static void load_dir_buf(uint32_t lba, uint8_t* buf) {
    ata_read_sectors(lba, FS_DIR_SECS, buf);
}
static fs_entry_t* root_dir(void) {
    if (!g_dir_loaded) { load_dir_buf(FS_DIR_LBA, g_dir); g_dir_loaded = 1; }
    return (fs_entry_t*)g_dir;
}
static void root_flush(void) { ata_write_sectors(FS_DIR_LBA, FS_DIR_SECS, g_dir); }

// 全局高水位分配: 扫描根 + 所有子目录, 返回数据区下一个空闲 LBA (按 span 跨段),
// 无空间返回 0。注意: 删除产生的空洞不回收 (与旧实现一致, 教学内核可接受)。
static uint32_t fs_alloc_lba(int span) {
    uint32_t hi = FS_DATA_LBA;
    fs_entry_t* d = root_dir();
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (d[i].used) { uint32_t e = d[i].lba + (uint32_t)span_of(&d[i]); if (e > hi) hi = e; }
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (d[i].used && d[i].type == FS_TYPE_DIR) {
            load_dir_buf(d[i].lba, g_scan);
            fs_entry_t* sd = (fs_entry_t*)g_scan;
            for (int j = 0; j < FS_MAX_FILES; j++)
                if (sd[j].used) { uint32_t e = sd[j].lba + (uint32_t)span_of(&sd[j]); if (e > hi) hi = e; }
        }
    }
    if (hi + (uint32_t)span > FS_DATA_LBA + FS_DATA_SECS) return 0;
    return hi;
}

static fs_entry_t* find_in(fs_entry_t* d, const char* name) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (d[i].used && fs_strcmp(d[i].name, name) == 0) return &d[i];
    return 0;
}

void fs_init(void) { load_dir_buf(FS_DIR_LBA, g_dir); g_dir_loaded = 1; }

int fs_count(void) {
    fs_entry_t* d = root_dir();
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) if (d[i].used) n++;
    return n;
}

int fs_list(char names[][FS_NAME_SZ], int max) {
    return fs_list_in(FS_DIR_LBA, names, 0, max);
}

// ---- 针对某个目录块的操作 ----
int fs_list_in(uint32_t dir_lba, char names[][FS_NAME_SZ], uint8_t types[], int max) {
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* d = (fs_entry_t*)g_tbuf;
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES && n < max; i++) {
        if (d[i].used) {
            fs_ncpy(names[n], d[i].name, FS_NAME_SZ);
            if (types) types[n] = d[i].type;
            n++;
        }
    }
    return n;
}

static int read_data(uint32_t lba, uint16_t nsec, char* buf, int cap) {
    if (nsec == 0) nsec = 1;
    if (nsec > FS_FILE_SECS) nsec = FS_FILE_SECS;
    fs_zero(g_io, FS_MAX_SIZE);
    if (ata_read_sectors(lba, (uint8_t)nsec, g_io) != 0) return -2;
    int len = 0;
    while (len < FS_MAX_SIZE && g_io[len]) len++;
    if (len > cap - 1) len = cap - 1;
    for (int i = 0; i < len; i++) buf[i] = (char)g_io[i];
    buf[len] = 0;
    return len;
}

int fs_read(const char* name, char* buf, int cap) {
    return fs_read_in(FS_DIR_LBA, name, buf, cap);
}
int fs_read_in(uint32_t dir_lba, const char* name, char* buf, int cap) {
    if (!buf || cap <= 0) return -1;
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* e = find_in((fs_entry_t*)g_tbuf, name);
    if (!e || e->type == FS_TYPE_DIR) return -1;   // 目录不可当作文件读
    return read_data(e->lba, e->nsec, buf, cap);
}

int fs_write(const char* name, const char* data) {
    return fs_write_in(FS_DIR_LBA, name, data);
}
int fs_write_in(uint32_t dir_lba, const char* name, const char* data) {
    if (!name || !name[0]) return -1;
    int len = fs_strlen(data ? data : "");
    if (len > FS_MAX_SIZE) len = FS_MAX_SIZE;

    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* d = (fs_entry_t*)g_tbuf;
    fs_entry_t* e = find_in(d, name);
    if (!e) {
        for (int i = 0; i < FS_MAX_FILES; i++)
            if (!d[i].used) { e = &d[i]; break; }
        if (!e) return -3;                              // 目录项已满
        fs_ncpy(e->name, name, FS_NAME_SZ);
        uint32_t lba = fs_alloc_lba(FS_FILE_SECS);
        if (lba == 0) return -4;                       // 数据区满
        e->lba = lba;
        e->nsec = 0;
        e->type = FS_TYPE_FILE;
        e->used = 1;
    } else if (e->type == FS_TYPE_DIR) {
        return -1;                                      // 同名目录, 拒绝覆盖
    }
    int nsec = (len + 511) / 512;
    if (nsec < 1) nsec = 1;
    if (nsec > FS_FILE_SECS) nsec = FS_FILE_SECS;
    fs_zero(g_io, FS_MAX_SIZE);
    for (int i = 0; i < len; i++) g_io[i] = (uint8_t)data[i];
    if (ata_write_sectors(e->lba, (uint8_t)nsec, g_io) != 0) return -2;
    e->nsec = (uint16_t)nsec;
    ata_write_sectors(dir_lba, FS_DIR_SECS, g_tbuf);    // 落盘目录块
    if (dir_lba == FS_DIR_LBA) g_dir_loaded = 0;        // 使根缓存失效, 下次读取重新载入
    return 0;
}

int fs_read_bin(const char* name, char* buf, int cap) {
    return fs_read_bin_in(FS_DIR_LBA, name, buf, cap);
}
int fs_read_bin_in(uint32_t dir_lba, const char* name, char* buf, int cap) {
    if (!buf || cap <= 0) return -1;
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* e = find_in((fs_entry_t*)g_tbuf, name);
    if (!e || e->type == FS_TYPE_DIR) return -1;
    fs_zero(g_io, FS_MAX_SIZE);
    if (ata_read_sectors(e->lba, (uint8_t)e->nsec, g_io) != 0) return -2;
    uint32_t len = (uint32_t)g_io[0] | ((uint32_t)g_io[1] << 8) |
                   ((uint32_t)g_io[2] << 16) | ((uint32_t)g_io[3] << 24);
    if (len == 0) len = (uint32_t)e->nsec * 512 - 4;   // 兼容旧文本文件 (无长度前缀)
    if (len > (uint32_t)cap - 1) len = (uint32_t)cap - 1;
    if (len > FS_MAX_SIZE - 4) len = FS_MAX_SIZE - 4;
    for (uint32_t i = 0; i < len; i++) buf[i] = (char)g_io[4 + i];
    buf[len] = 0;
    return (int)len;
}

int fs_write_bin(const char* name, const char* data, int len) {
    return fs_write_bin_in(FS_DIR_LBA, name, data, len);
}
int fs_write_bin_in(uint32_t dir_lba, const char* name, const char* data, int len) {
    if (!name || !name[0] || !data || len < 0) return -1;
    if (len > FS_MAX_SIZE - 4) len = FS_MAX_SIZE - 4;
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* d = (fs_entry_t*)g_tbuf;
    fs_entry_t* e = find_in(d, name);
    if (!e) {
        for (int i = 0; i < FS_MAX_FILES; i++)
            if (!d[i].used) { e = &d[i]; break; }
        if (!e) return -3;                                // 目录项已满
        fs_ncpy(e->name, name, FS_NAME_SZ);
        uint32_t lba = fs_alloc_lba(FS_FILE_SECS);
        if (lba == 0) return -4;                          // 数据区满
        e->lba = lba;
        e->nsec = 0;
        e->type = FS_TYPE_FILE;
        e->used = 1;
    } else if (e->type == FS_TYPE_DIR) {
        return -1;                                         // 同名目录, 拒绝覆盖
    }
    int nsec = (len + 4 + 511) / 512;
    if (nsec < 1) nsec = 1;
    if (nsec > FS_FILE_SECS) nsec = FS_FILE_SECS;
    fs_zero(g_io, FS_MAX_SIZE);
    g_io[0] = (uint8_t)(len & 0xFF);
    g_io[1] = (uint8_t)((len >> 8) & 0xFF);
    g_io[2] = (uint8_t)((len >> 16) & 0xFF);
    g_io[3] = (uint8_t)((len >> 24) & 0xFF);
    for (int i = 0; i < len; i++) g_io[4 + i] = (uint8_t)data[i];
    if (ata_write_sectors(e->lba, (uint8_t)nsec, g_io) != 0) return -2;
    e->nsec = (uint16_t)nsec;
    ata_write_sectors(dir_lba, FS_DIR_SECS, g_tbuf);
    if (dir_lba == FS_DIR_LBA) g_dir_loaded = 0;   // 使根缓存失效, 下次读取重新载入
    return 0;
}

int fs_remove(const char* name) { return fs_remove_in(FS_DIR_LBA, name); }
int fs_remove_in(uint32_t dir_lba, const char* name) {
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* e = find_in((fs_entry_t*)g_tbuf, name);
    if (!e || e->type == FS_TYPE_DIR) return -1;
    e->used = 0;
    e->nsec = 0;
    ata_write_sectors(dir_lba, FS_DIR_SECS, g_tbuf);
    if (dir_lba == FS_DIR_LBA) g_dir_loaded = 0;   // 使根缓存失效, 下次读取重新载入
    return 0;
}

int fs_rename(const char* old, const char* new) { return fs_rename_in(FS_DIR_LBA, old, new); }
int fs_rename_in(uint32_t dir_lba, const char* old, const char* new) {
    if (!old || !new || !old[0] || !new[0]) return -1;
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* d = (fs_entry_t*)g_tbuf;
    fs_entry_t* e = find_in(d, old);
    if (!e) return -1;
    if (find_in(d, new)) return -3;                // 目标名已存在
    fs_ncpy(e->name, new, FS_NAME_SZ);
    ata_write_sectors(dir_lba, FS_DIR_SECS, g_tbuf);
    if (dir_lba == FS_DIR_LBA) g_dir_loaded = 0;
    return 0;
}

int fs_size(const char* name) { return fs_size_in(FS_DIR_LBA, name); }
int fs_size_in(uint32_t dir_lba, const char* name) {
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* e = find_in((fs_entry_t*)g_tbuf, name);
    if (!e) return -1;
    if (e->type == FS_TYPE_DIR) return 0;               // 目录无字节大小
    return read_data(e->lba, e->nsec, g_io, FS_MAX_SIZE); // 复用 g_io 取长度
}

int fs_stats(int* used_entries, int* used_sectors) {
    return fs_stats_in(FS_DIR_LBA, used_entries, used_sectors);
}
int fs_stats_in(uint32_t dir_lba, int* used_entries, int* used_sectors) {
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* d = (fs_entry_t*)g_tbuf;
    int ue = 0, us = 0;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (d[i].used) { ue++; us += d[i].nsec ? d[i].nsec : 1; }
    if (used_entries) *used_entries = ue;
    if (used_sectors) *used_sectors = us;
    return ue;
}

int fs_stats_total(int* used_entries, int* used_sectors) {
    int ue = 0, us = 0;
    fs_stats_in(FS_DIR_LBA, &ue, &us);
    fs_entry_t* d = root_dir();
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (d[i].used && d[i].type == FS_TYPE_DIR) {
            int se = 0, ss = 0;
            fs_stats_in(d[i].lba, &se, &ss);
            ue += se; us += ss + FS_DIR_SECS;   // 子目录本身也占 FS_DIR_SECS
        }
    }
    if (used_entries) *used_entries = ue;
    if (used_sectors) *used_sectors = us;
    return ue;
}

int fs_mkdir(uint32_t dir_lba, const char* name) {
    if (!name || !name[0]) return -1;
    if (fs_strlen(name) >= FS_NAME_SZ) return -1;
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* d = (fs_entry_t*)g_tbuf;
    if (find_in(d, name)) return -1;                     // 重名
    fs_entry_t* e = 0;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (!d[i].used) { e = &d[i]; break; }
    if (!e) return -3;                                   // 目录项已满
    uint32_t lba = fs_alloc_lba(FS_DIR_SECS);
    if (lba == 0) return -4;                             // 数据区满
    // 清零新目录块
    fs_zero(g_scan, FS_DIR_SECS * 512);
    ata_write_sectors(lba, FS_DIR_SECS, g_scan);
    fs_ncpy(e->name, name, FS_NAME_SZ);
    e->lba = lba;
    e->nsec = FS_DIR_SECS;
    e->type = FS_TYPE_DIR;
    e->used = 1;
    ata_write_sectors(dir_lba, FS_DIR_SECS, g_tbuf);
    if (dir_lba == FS_DIR_LBA) g_dir_loaded = 0;   // 使根缓存失效, 下次读取重新载入
    return 0;
}

int fs_rmdir(uint32_t dir_lba, const char* name) {
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* e = find_in((fs_entry_t*)g_tbuf, name);
    if (!e || e->type != FS_TYPE_DIR) return -1;
    load_dir_buf(e->lba, g_scan);
    fs_entry_t* sd = (fs_entry_t*)g_scan;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (sd[i].used) return -2;                       // 非空, 拒绝删除
    e->used = 0;
    e->nsec = 0;
    ata_write_sectors(dir_lba, FS_DIR_SECS, g_tbuf);
    if (dir_lba == FS_DIR_LBA) g_dir_loaded = 0;   // 使根缓存失效, 下次读取重新载入
    return 0;
}

uint32_t fs_subdir_lba(uint32_t dir_lba, const char* name) {
    load_dir_buf(dir_lba, g_tbuf);
    fs_entry_t* e = find_in((fs_entry_t*)g_tbuf, name);
    if (!e || e->type != FS_TYPE_DIR) return 0;
    return e->lba;
}

void fs_format(void) {
    fs_zero(g_dir, FS_DIR_SECS * 512);
    g_dir_loaded = 1;
    root_flush();
}

// 供编辑器做 buf 内插入/删除 (与文件无关, 放这里是因为两处都要用同一套助手)
void fs_buf_move(void* d, const void* s, int n) { fs_mmove(d, s, n); }
