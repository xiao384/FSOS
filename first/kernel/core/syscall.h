// syscall.h - 系统调用机制 (syscall/sysret)
// 用户态程序用 System V ABI 触发 syscall (rax=号, rdi/rsi/r10/r8/r9=参数),
// syscall_entry 汇编把参数转入内核的 MS x64 ABI 再调用 handle_syscall。
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

// 系统调用号
#define SYS_NULL       0   // 测试: 返回固定值
#define SYS_WRITE      1   // write(fd, buf, len) —— 前 2 参数: buf, len (fd 暂忽略)
#define SYS_GET_TICKS  2   // 返回自启动以来的毫秒数
#define SYS_WRITE_MSG  3   // 输出预定义消息 (a1=消息索引), 供位置无关用户代码使用
#define SYS_EXIT       4   // exit(code) —— 标记当前线程终止, 不返回
#define SYS_OPEN       5   // open(name) —— 打开文件, 返回 fd (>=3), -1+ 不存在
#define SYS_READ       6   // read(fd, buf, len) —— 读文件到 buf, 返回字节数
#define SYS_CLOSE      7   // close(fd) —— 关闭文件, 返回 0
#define SYS_GETPID     8   // 返回当前进程 pid
#define SYS_LSEEK      9   // lseek(fd, offset, whence) —— 移动读写位置, 返回新位置
#define SYS_FSTAT      10  // fstat(fd) —— 返回文件大小 (简化版, 无 stat 结构)
#define SYS_LIST       11  // list(buf, len) —— 列出所有文件名到 buf, 返回字节数
#define SYS_SPAWN      12  // spawn(name) —— 从 ramfs 加载 ELF 创建新进程, 返回子 PID
#define SYS_WAIT       13  // wait(pid) —— 等待指定进程退出, 返回 0=成功, -1=不存在
#define SYS_LINUX_EXEC 14  // linux_exec(path) —— 从 FS 加载 Linux ELF 作为 ring3 进程, 返回 pid

// 初始化: 启用 IA32_EFER.SCE, 配置 IA32_STAR/IA32_LSTAR/IA32_SFMASK
void syscall_init(void);

// 设置 syscall 内核栈 (调度器切换进程时调用)
void syscall_set_kernel_rsp(uint64_t rsp);

// 内核侧系统调用分发 (MS x64 ABI: num 在 rcx, a1 在 rdx, a2 在 r8, a3 在 r9,
//                       a4 在 [rsp+32], a5 在 [rsp+40], a6 在 [rsp+48])
// Linux 兼容进程 (sched_current_is_linux) 的请求会转发到 linux_syscall。
uint64_t handle_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6);

// 从 ring0 切换到 ring3 执行用户态代码 (iretq, syscall_asm.asm)
// 复制位置无关的用户代码模板到 USER_CODE_BASE, 然后跳转执行
void enter_ring3_test(void);

#endif // SYSCALL_H