// syscall.c - 系统调用机制初始化与分发
#include <stdint.h>
#include "syscall.h"
#include "io.h"     // inb/outb (串口调试)
#include "idt.h"    // get_ticks()
#include "sched.h"  // sched_exit() (SYS_EXIT)
#include "ramfs.h"  // ramfs_* (SYS_OPEN/READ/CLOSE, 阶段 4)
#include "elf.h"    // elf_load (SYS_SPAWN, 阶段 4)
#include "paging.h" // USER_STACK_TOP (SYS_SPAWN, 阶段 4)
#include "linux.h"  // linux_exec / linux_syscall (Linux 兼容层)

// 内核栈顶 (start.asm 导出)
extern char stack_top[];

// syscall entry (syscall_asm.asm) 使用的内核栈指针
uint64_t g_syscall_kernel_rsp = 0;

// 设置 syscall 内核栈 (调度器切换进程时调用)
void syscall_set_kernel_rsp(uint64_t rsp) {
    g_syscall_kernel_rsp = rsp;
}

// ---- MSR 读写 ----
static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

// 串口输出 (syscall write 的调试落点)
static void ser_putc(char c) {
    while ((inb(0x3FD) & 0x20) == 0) {}
    outb(0x3F8, (uint8_t)c);
}
static void ser_puts(const char* s) {
    for (; *s; ++s) ser_putc(*s);
}

void syscall_init(void) {
    extern void syscall_entry(void);

    g_syscall_kernel_rsp = (uint64_t)(uintptr_t)stack_top;


    // 1. 启用 syscall/sysret (IA32_EFER.SCE = bit 0)
    wrmsr(0xC0000080, rdmsr(0xC0000080) | 1);

    // 2. IA32_STAR:
    //    [47:32] = 内核 CS (0x08)  -> syscall 进入时 CS=0x08, SS=0x10
    //    [63:48] = 0x10            -> sysret 返回时 CS=0x10+16=0x23(用户代码), SS=0x10+8=0x1B(用户数据)
    wrmsr(0xC0000081, (0x10ULL << 48) | (0x08ULL << 32));

    // 3. IA32_LSTAR: syscall 入口地址
    wrmsr(0xC0000082, (uint64_t)(uintptr_t)syscall_entry);

    // 4. IA32_SFMASK: 进入时清 RFLAGS.IF (关中断, 防重入)
    wrmsr(0xC0000084, 0x200);
}

uint64_t handle_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    // Linux 兼容进程: 同一 `syscall` 指令转发到 Linux ABI 翻译层
    if (sched_current_is_linux())
        return linux_syscall(num, a1, a2, a3, a4, a5, a6);

    switch (num) {
        case SYS_NULL:

            return 0x12345678ULL;
        case SYS_WRITE: {
            const char* p = (const char*)(uintptr_t)a1;
            uint64_t len = a2;
            for (uint64_t i = 0; i < len && p[i]; i++) ser_putc(p[i]);
            return len;
        }
        case SYS_GET_TICKS:
            return (uint64_t)get_ticks();
        case SYS_WRITE_MSG: {
            // 预定义消息表, 供位置无关用户代码使用
            static const char* msgs[] = {
                "Hello from ring3 (isolated)!\r\n",
                "User code running in ring3!\r\n",
                "Syscall roundtrip OK!\r\n",
            };
            if (a1 < 3) { ser_puts(msgs[a1]); return 0; }
            return (uint64_t)-1;
        }
        case SYS_EXIT:
            sched_exit();   // 标记当前线程 DEAD, 不返回
            return 0;
        case SYS_OPEN: {
            const char* name = (const char*)(uintptr_t)a1;
            int fd = ramfs_open(name);
            return (uint64_t)(int64_t)fd;
        }
        case SYS_READ: {
            int fd = (int)(int64_t)a1;
            void* buf = (void*)(uintptr_t)a2;
            int len = (int)(int64_t)a3;
            int n = ramfs_read(fd, buf, len);
            return (uint64_t)(int64_t)n;
        }
        case SYS_CLOSE: {
            int fd = (int)(int64_t)a1;
            int r = ramfs_close(fd);
            return (uint64_t)(int64_t)r;
        }
        case SYS_GETPID:

            return (uint64_t)(int64_t)sched_current_pid();
        case SYS_LSEEK: {
            int fd = (int)(int64_t)a1;
            int offset = (int)(int64_t)a2;
            int whence = (int)(int64_t)a3;
            return (uint64_t)(int64_t)ramfs_lseek(fd, offset, whence);
        }
        case SYS_FSTAT: {
            int fd = (int)(int64_t)a1;
            return (uint64_t)(int64_t)ramfs_size(fd);
        }
        case SYS_LIST: {
            char* buf = (char*)(uintptr_t)a1;
            int len = (int)(int64_t)a2;
            return (uint64_t)(int64_t)ramfs_list(buf, len);
        }
        case SYS_SPAWN: {
            uint64_t entry = sched_current_entry();
            if (!entry) return (uint64_t)-1;
            uint64_t child_rsp = sched_current_user_rsp() + 0x20000;
            int pid = sched_create_user_process(entry, child_rsp, "spawn");
            return (uint64_t)(int64_t)pid;
        }
        case SYS_WAIT: {
            int pid = (int)(int64_t)a1;
            while (!sched_is_dead(pid)) {
                __asm__ volatile("sti; hlt");
            }
            return 0;
        }
        case SYS_LINUX_EXEC: {
            const char* path = (const char*)(uintptr_t)a1;
            int pid = linux_exec(path);
            return (uint64_t)(int64_t)pid;
        }
        default:
            return (uint64_t)-1;
    }
}