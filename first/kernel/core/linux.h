// linux.h - Linux 二进制兼容层 (Linuxulator) 接口
//
// 让 FSOS 能加载并运行 Linux x86-64 用户态 ELF (当前目标: 控制台程序子集,
// 优先支持 PIE/ET_DYN 静态链接二进制)。实现见 linux.c。
#ifndef LINUX_H
#define LINUX_H

#include <stdint.h>

// 从 FSOS 文件系统按名加载一个 Linux ELF 并作为 ring3 用户进程启动。
// 成功返回新进程 pid (>0), 失败返回负错误码:
//   -1 内存不足  -2 文件不存在  -3 非合法 Linux ELF
//   -4 仅支持 PIE(ET_DYN)  -5 页表创建失败  -6 物理帧耗尽
int  linux_exec(const char* path);

// Linux 系统调用分发 (由 syscall.c 在 is_linux 进程中转发至此)。
// Linux x86-64 ABI: rax=号, rdi,rsi,rdx,r10,r8,r9 = 参数。
uint64_t linux_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                      uint64_t a4, uint64_t a5, uint64_t a6);

#endif // LINUX_H
