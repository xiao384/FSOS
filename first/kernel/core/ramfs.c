// ramfs.c - 内存文件系统实现 (阶段 4)
#include "ramfs.h"
#include "kheap.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char    name[RAMFS_MAX_NAME];
    uint8_t data[RAMFS_FILE_SIZE];
    int     size;
    int     in_use;
} ramfs_file_t;

typedef struct {
    int file_idx;   // 指向 g_files 的索引, -1=空闲
    int pos;        // 当前读写位置
} ramfs_fd_t;

static ramfs_file_t g_files[RAMFS_MAX_FILES];
static ramfs_fd_t   g_fds[RAMFS_MAX_FDS];
static int          g_inited = 0;

static int str_eq(const char* a, const char* b) {
    int i; for (i = 0; i < RAMFS_MAX_NAME && a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == 0 && b[i] == 0;
}
static void str_copy(char* dst, const char* src) {
    int i; for (i = 0; i < RAMFS_MAX_NAME - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

void ramfs_init(void) {
    if (g_inited) return;
    g_inited = 1;

    for (int i = 0; i < RAMFS_MAX_FILES; i++) g_files[i].in_use = 0;
    for (int i = 0; i < RAMFS_MAX_FDS; i++) g_fds[i].file_idx = -1;

    // 预置测试文件
    // hello.txt
    {
        ramfs_file_t* f = &g_files[0];
        str_copy(f->name, "hello.txt");
        const char* msg = "Hello from ramfs!\n";
        int i; for (i = 0; msg[i]; i++) f->data[i] = (uint8_t)msg[i];
        f->size = i;
        f->in_use = 1;
    }
    // version.txt
    {
        ramfs_file_t* f = &g_files[1];
        str_copy(f->name, "version.txt");
        const char* msg = "FSOS v0.2 stage 4\n";
        int i; for (i = 0; msg[i]; i++) f->data[i] = (uint8_t)msg[i];
        f->size = i;
        f->in_use = 1;
    }
}

int ramfs_open(const char* name) {
    if (!g_inited) return -1;
    // 查找文件
    int fi = -1;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (g_files[i].in_use && str_eq(g_files[i].name, name)) { fi = i; break; }
    }
    if (fi < 0) return -1;
    // 分配 fd
    for (int i = 0; i < RAMFS_MAX_FDS; i++) {
        if (g_fds[i].file_idx < 0) {
            g_fds[i].file_idx = fi;
            g_fds[i].pos = 0;
            return RAMFS_FD_BASE + i;
        }
    }
    return -2;
}

int ramfs_read(int fd, void* buf, int len) {
    if (!g_inited) return -1;
    int fi = fd - RAMFS_FD_BASE;
    if (fi < 0 || fi >= RAMFS_MAX_FDS) return -1;
    if (g_fds[fi].file_idx < 0) return -1;

    ramfs_file_t* f = &g_files[g_fds[fi].file_idx];
    int avail = f->size - g_fds[fi].pos;
    if (avail <= 0) return 0;
    if (len > avail) len = avail;
    uint8_t* dst = (uint8_t*)buf;
    for (int i = 0; i < len; i++) dst[i] = f->data[g_fds[fi].pos + i];
    g_fds[fi].pos += len;
    return len;
}

int ramfs_write(int fd, const void* buf, int len) {
    if (!g_inited) return -1;
    int fi = fd - RAMFS_FD_BASE;
    if (fi < 0 || fi >= RAMFS_MAX_FDS) return -1;
    if (g_fds[fi].file_idx < 0) return -1;

    ramfs_file_t* f = &g_files[g_fds[fi].file_idx];
    int avail = RAMFS_FILE_SIZE - g_fds[fi].pos;
    if (avail <= 0) return 0;
    if (len > avail) len = avail;
    const uint8_t* src = (const uint8_t*)buf;
    for (int i = 0; i < len; i++) f->data[g_fds[fi].pos + i] = src[i];
    g_fds[fi].pos += len;
    if (g_fds[fi].pos > f->size) f->size = g_fds[fi].pos;
    return len;
}

int ramfs_close(int fd) {
    if (!g_inited) return -1;
    int fi = fd - RAMFS_FD_BASE;
    if (fi < 0 || fi >= RAMFS_MAX_FDS) return -1;
    if (g_fds[fi].file_idx < 0) return -1;
    g_fds[fi].file_idx = -1;
    return 0;
}

int ramfs_create(const char* name) {
    if (!g_inited) return -1;
    // 检查是否已存在
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (g_files[i].in_use && str_eq(g_files[i].name, name)) return -2;
    }
    // 分配新文件
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!g_files[i].in_use) {
            str_copy(g_files[i].name, name);
            g_files[i].size = 0;
            g_files[i].in_use = 1;
            return 0;
        }
    }
    return -1;
}

int ramfs_lseek(int fd, int offset, int whence) {
    if (!g_inited) return -1;
    int fi = fd - RAMFS_FD_BASE;
    if (fi < 0 || fi >= RAMFS_MAX_FDS) return -1;
    if (g_fds[fi].file_idx < 0) return -1;
    ramfs_file_t* f = &g_files[g_fds[fi].file_idx];
    int newpos;
    switch (whence) {
        case 0: newpos = offset; break;              // SEEK_SET
        case 1: newpos = g_fds[fi].pos + offset; break;  // SEEK_CUR
        case 2: newpos = f->size + offset; break;    // SEEK_END
        default: return -1;
    }
    if (newpos < 0 || newpos > f->size) return -1;
    g_fds[fi].pos = newpos;
    return newpos;
}

int ramfs_size(int fd) {
    if (!g_inited) return -1;
    int fi = fd - RAMFS_FD_BASE;
    if (fi < 0 || fi >= RAMFS_MAX_FDS) return -1;
    if (g_fds[fi].file_idx < 0) return -1;
    return g_files[g_fds[fi].file_idx].size;
}

int ramfs_list(char* buf, int buf_len) {
    if (!g_inited || buf_len <= 0) return -1;
    int pos = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!g_files[i].in_use) continue;
        const char* nm = g_files[i].name;
        int j;
        for (j = 0; nm[j]; j++) {
            if (pos + j >= buf_len - 1) return -1;
            buf[pos + j] = nm[j];
        }
        pos += j;
        if (pos >= buf_len - 1) return -1;
        buf[pos++] = '\n';
    }
    if (pos < buf_len) buf[pos] = 0;
    return pos;
}

int ramfs_load(const char* name, const void* data, int size) {
    if (!g_inited || size > RAMFS_FILE_SIZE) return -1;
    int fi = -1;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (g_files[i].in_use && str_eq(g_files[i].name, name)) { fi = i; break; }
    }
    if (fi < 0) {
        for (int i = 0; i < RAMFS_MAX_FILES; i++) {
            if (!g_files[i].in_use) { fi = i; break; }
        }
    }
    if (fi < 0) return -1;
    str_copy(g_files[fi].name, name);
    const uint8_t* src = (const uint8_t*)data;
    for (int i = 0; i < size; i++) g_files[fi].data[i] = src[i];
    g_files[fi].size = size;
    g_files[fi].in_use = 1;
    return 0;
}