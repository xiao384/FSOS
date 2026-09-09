// sched.h - 抢占式内核线程调度器 (阶段 2)
//
// 时钟中断 (IRQ0, 1000Hz) 驱动轮转调度。在 isr_common 的 idt_dispatch
// 返回后调用 sched_tick_isr: 保存当前栈帧指针到 PCB, 选下一个 READY 线程,
// 返回其 saved_rsp; isr_common 把它加载到 rsp 后 pop + iretq 即完成切换。
//
// 线程创建时在内核栈上伪造一个中断栈帧 (RIP=entry, CS=内核段, RFLAGS=IF=1),
// 首次切换到它时 iretq "返回"到线程入口。
#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

// 线程状态
typedef enum {
    TS_READY,      // 可运行
    TS_RUNNING,    // 正在 CPU 上
    TS_DEAD        // 已终止 (调度器跳过)
} thread_state_t;

// 进程控制块
typedef struct pcb {
    int            pid;
    char           name[16];
    thread_state_t state;
    void*          kstack_base;   // kmalloc 分配的内核栈 (NULL=主线程用 start.asm 栈)
    uint32_t       kstack_size;   // 栈大小
    uint64_t       saved_rsp;     // 中断栈帧指针 (isr_common 的 rsp, 切换锚点)
    uint64_t       kernel_rsp0;   // TSS.rsp0 (用户态中断进入内核的栈顶)
    int            is_user;       // 1=用户态进程(ring3), 0=内核线程(ring0)
    int            is_linux;      // 1=Linux 兼容进程 (syscall 转发到 linux_syscall)
    int            quantum;       // 剩余时间片 (tick 数)
    uint64_t       pml4_phys;     // 用户进程独立页表 PML4 物理地址 (0=用初始页表)
    uint64_t       entry;         // 用户进程入口 (SYS_SPAWN 用)
    uint64_t       user_rsp;      // 用户栈顶 (SYS_SPAWN 分配独立栈)
} pcb_t;

// 初始化调度器: 当前执行流成为 PID 0 (主线程/GUI), 开抢占
void sched_init(void);

// 创建内核线程: entry 为入口 (不返回; 内部应调 sched_exit 或死循环)
// 返回 pid, -1 失败
int  sched_create_thread(void (*entry)(void), const char* name);

// 创建用户态进程 (ring3): entry = 用户虚拟地址, user_rsp = 用户栈顶
// 分配独立内核栈, 伪造用户态中断栈帧 (CS=0x23, SS=0x1B)
// 返回 pid, -1 失败
int  sched_create_user_process(uint64_t entry, uint64_t user_rsp, const char* name);

// 同上, 但使用调用方已建好的页表 pml4, 并标记 is_linux (Linux 兼容进程)
// 返回 pid, -1 失败
int  sched_create_user_process_ex(uint64_t pml4, uint64_t entry, uint64_t user_rsp,
                                  const char* name, int is_linux);

// 标记当前线程终止, 永久挂起让出 CPU (不返回)
void sched_exit(void) __attribute__((noreturn));

// pit_handler 调用: 标记本次 tick 需要调度检查
void sched_request(void);

// isr_common 调用: cur_rsp = 当前中断栈帧指针
// 返回新栈帧指针 (需切换), 0 = 不切换
uint64_t sched_tick_isr(uint64_t cur_rsp);

// 调试: 当前线程 pid
int  sched_current_pid(void);

// 当前线程是否为 Linux 兼容进程 (syscall 分发用)
int  sched_current_is_linux(void);

// 调试: 线程数
int  sched_thread_count(void);

// 检查指定 pid 是否已终止 (阶段 4: SYS_WAIT)
int  sched_is_dead(int pid);

// 获取当前进程的 entry (阶段 4: SYS_SPAWN)
uint64_t sched_current_entry(void);

// 获取当前进程的用户栈顶 (阶段 4: SYS_SPAWN)
uint64_t sched_current_user_rsp(void);

// 创建 2 个测试线程验证多线程切换 (kernel.c 调用)
void sched_run_test(void);

#endif // SCHED_H