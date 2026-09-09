// ramfs.h - 内存文件系统 (阶段 4)
//
// 简单扁平文件系统, 数据在内核堆. 供 syscall SYS_OPEN/READ/WRITE/CLOSE 使用.
// 每进程最多 MAX_FDS 个打开文件描述符, fd 从 3 开始 (0/1/2 预留给 stdin/stdout/stderr).
#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>
#include <stddef.h>

#define RAMFS_MAX_FILES  16      // 最多文件数
#define RAMFS_MAX_FDS    8       // 每进程最多打开文件数
#define RAMFS_MAX_NAME   32      // 文件名最大长度
#define RAMFS_FILE_SIZE  4096    // 每文件最大容量
#define RAMFS_FD_BASE    3       // fd 从 3 开始 (0/1/2 预留)

// 初始化: 预置几个测试文件
void ramfs_init(void);

// 打开文件: 返回 fd (>=3), -1 不存在, -2 fd 表满
int  ramfs_open(const char* name);

// 读文件: 返回实际读取字节数, 0=EOF, -1=无效 fd
int  ramfs_read(int fd, void* buf, int len);

// 写文件: 返回实际写入字节数, -1=无效 fd, -2=只读
int  ramfs_write(int fd, const void* buf, int len);

// 关闭文件: 0=成功, -1=无效 fd
int  ramfs_close(int fd);

// 创建文件: 返回 0=成功, -1=已满, -2=已存在
int  ramfs_create(const char* name);

// 移动读写位置: whence 0=SET, 1=CUR, 2=END; 返回新位置, -1=错误
int  ramfs_lseek(int fd, int offset, int whence);

// 查询文件大小: 返回字节数, -1=无效 fd
int  ramfs_size(int fd);

// 列出所有文件名到 buf (换行分隔); 返回写入字节数, -1=buf 太小
int  ramfs_list(char* buf, int buf_len);

// 从内存加载文件数据: 创建文件并写入 (覆盖已有); 返回 0=成功, -1=失败
int  ramfs_load(const char* name, const void* data, int size);

#endif // RAMFS_H