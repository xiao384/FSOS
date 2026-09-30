// linux.h - Linux 二进制兼容层 (Linuxulator) 接口
//
// 让 FSOS 能加载并运行 Linux x86-64 用户态 ELF (当前目标: 控制台程序子集,
// 优先支持 PIE/ET_DYN 静态链接二进制)。实现见 linux.c。
#ifndef LINUX_H
#define LINUX_H

#include <stdint.h>

// per-process Linux 上下文 (brk/mmap 游标隔离)
typedef struct {
    int      in_use;       // 1=已登记的 Linux 进程
    uint64_t brk;          // 当前 brk 断点
    uint64_t mmap_next;    // 下一个 mmap 分配地址
} linux_ctx_t;

// 从 FSOS 文件系统按名加载一个 Linux ELF 并作为 ring3 用户进程启动。
// 成功返回新进程 pid (>0), 失败返回负错误码:
//   -1 内存不足  -2 文件不存在  -3 非合法 Linux ELF
//   -4 仅支持 PIE(ET_DYN)  -5 页表创建失败  -6 物理帧耗尽
int  linux_exec(const char* path);

// 启动期确保 FS 中存在 HELLO.ELF (内嵌于内核的 Linuxulator 演示程序)。
void linux_ensure_hello(void);
// 可选自检: 若宏 LINUX_SELFTEST 开启, 自动运行 HELLO.ELF 并向串口打印结果。
void linux_selftest(void);

// Linux 系统调用分发 (由 syscall.c 在 is_linux 进程中转发至此)。
// Linux x86-64 ABI: rax=号, rdi,rsi,rdx,r10,r8,r9 = 参数。
uint64_t linux_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6);

// per-process 上下文管理
linux_ctx_t* linux_ctx_current(void);       // 取当前 Linux 进程 ctx (无则返回 NULL)
void         linux_ctx_free_current(void);   // 当前进程退出时回收 ctx

#endif // LINUX_H
