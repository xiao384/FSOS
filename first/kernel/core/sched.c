// sched.c - 抢占式内核线程调度器实现 (阶段 2)
#include "sched.h"
#include "kheap.h"
#include "gdt.h"
#include "paging.h"   // 独立页表 (阶段 4)
#include "syscall.h"  // syscall_set_kernel_rsp (阶段 4)
#include "io.h"       // inb/outb (static inline)
#include <stdint.h>
#include <stddef.h>

extern char stack_top[];   // start.asm 的内核栈顶 (主线程)

#define MAX_THREADS    16
#define KSTACK_SIZE    (16 * 1024)      // 16 KB 内核栈
#define QUANTUM_TICKS  5                // 5 ms 时间片 (PIT 1000Hz)

static pcb_t   g_threads[MAX_THREADS];
static int     g_thread_count = 0;
static pcb_t*  g_current = NULL;
static int     g_sched_inited = 0;
static volatile int g_sched_pending = 0;   // pit_handler 设, sched_tick_isr 清

// ---- 串口输出 (测试线程用, 不依赖 kernel.c 的 static 函数) ----
static void s_putc(char c) {
    for (unsigned i = 0; i < 0x2000; ++i) {
        if (inb(0x3FD) & 0x20) { outb(0x3F8, (uint8_t)c); return; }
    }
}
static void s_puts(const char* s) { for (; *s; ++s) s_putc(*s); }
static void s_putd(int n) {
    char buf[12]; int i = 0;
    if (n < 0) { s_putc('-'); n = -n; }
    if (n == 0) { s_putc('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) s_putc(buf[--i]);
}

// ============================================================
// 初始化: 当前执行流成为 PID 0 (主线程)
// ============================================================
void sched_init(void) {
    if (g_sched_inited) return;
    g_sched_inited = 1;

    pcb_t* p = &g_threads[0];
    p->pid = 0;
    p->name[0]='m'; p->name[1]='a'; p->name[2]='i'; p->name[3]='n';
    p->name[4]=0;
    p->state = TS_RUNNING;
    p->kstack_base = NULL;       // 主线程用 start.asm 的 64KB BSS 栈
    p->kstack_size = 0;
    p->saved_rsp = 0;            // 首次中断时由 sched_tick_isr 填入
    p->kernel_rsp0 = (uint64_t)(uintptr_t)stack_top;  // 主线程用 start.asm 栈
    p->is_user = 0;
    p->quantum = QUANTUM_TICKS;

    g_thread_count = 1;
    g_current = p;
    s_puts("[sched] init: main thread pid=0\r\n");
}

// ============================================================
// 创建内核线程: 分配栈, 伪造中断栈帧
// ============================================================
int sched_create_thread(void (*entry)(void), const char* name) {
    if (!g_sched_inited || g_thread_count >= MAX_THREADS) return -1;

    void* kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) return -1;

    // 栈顶 16 字节对齐 (C ABI 要求)
    uintptr_t kstack_top = (uintptr_t)kstack + KSTACK_SIZE;
    kstack_top &= ~((uintptr_t)15);

    // 从栈顶往下伪造中断栈帧 (与 isr_common 的 pop 顺序匹配)
    // 栈帧: [r15..rax(15), vector, dummy, RIP, CS, RFLAGS, RSP, SS]
    uint64_t* sp = (uint64_t*)kstack_top;
    sp--; *sp = GDT_KERNEL_DS;            // SS  (0x10, ring0 数据段)
    sp--; *sp = (uint64_t)kstack_top;     // RSP (iretq 后线程栈顶)
    sp--; *sp = 0x202;                     // RFLAGS (IF=1, 允许抢占)
    sp--; *sp = GDT_KERNEL_CS;            // CS  (0x08, ring0 代码段)
    sp--; *sp = (uint64_t)(uintptr_t)entry; // RIP
    sp--; *sp = 0;                         // dummy error code
    sp--; *sp = 32;                        // vector (IRQ0, 仅占位)
    for (int i = 0; i < 15; i++) { sp--; *sp = 0; }  // r15..rax = 0

    pcb_t* p = &g_threads[g_thread_count];
    p->pid = g_thread_count;
    p->kstack_base = kstack;
    p->kstack_size = KSTACK_SIZE;
    p->saved_rsp = (uint64_t)(uintptr_t)sp;
    p->kernel_rsp0 = (uint64_t)kstack_top;   // 内核线程的 TSS.rsp0
    p->is_user = 0;
    p->state = TS_READY;
    p->quantum = QUANTUM_TICKS;
    // 复制名字
    int j; for (j = 0; j < 15 && name && name[j]; j++) p->name[j] = name[j];
    p->name[j] = 0;

    g_thread_count++;
    s_puts("[sched] created thread pid=");
    s_putd(p->pid);
    s_puts(" name=");
    s_puts(p->name);
    s_puts("\r\n");
    return p->pid;
}

// ============================================================
// 创建用户态进程 (ring3): 独立内核栈 + 用户态中断栈帧
// ============================================================
int sched_create_user_process(uint64_t entry, uint64_t user_rsp, const char* name) {
    uint64_t pml4 = paging_clone_kernel();
    if (!pml4) return -1;
    return sched_create_user_process_ex(pml4, entry, user_rsp, name, 0);
}

int sched_create_user_process_ex(uint64_t pml4, uint64_t entry, uint64_t user_rsp,
                                 const char* name, int is_linux) {
    if (!g_sched_inited || g_thread_count >= MAX_THREADS) return -1;

    void* kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) return -1;

    uintptr_t kstack_top = (uintptr_t)kstack + KSTACK_SIZE;
    kstack_top &= ~((uintptr_t)15);

    // 伪造用户态中断栈帧 (CS=0x23 ring3 代码, SS=0x1B ring3 数据)
    uint64_t* sp = (uint64_t*)kstack_top;
    sp--; *sp = GDT_USER_DS | 3;            // SS = 0x1B (用户数据段 ring3)
    sp--; *sp = user_rsp;                    // RSP = 用户栈顶
    sp--; *sp = 0x202;                        // RFLAGS (IF=1)
    sp--; *sp = GDT_USER_CS | 3;            // CS = 0x23 (用户代码段 ring3)
    sp--; *sp = entry;                        // RIP = ELF 入口
    sp--; *sp = 0;                            // dummy error code
    sp--; *sp = 32;                           // vector (IRQ0, 占位)
    for (int i = 0; i < 15; i++) { sp--; *sp = 0; }  // r15..rax = 0

    pcb_t* p = &g_threads[g_thread_count];
    p->pid = g_thread_count;
    p->kstack_base = kstack;
    p->kstack_size = KSTACK_SIZE;
    p->saved_rsp = (uint64_t)(uintptr_t)sp;
    p->kernel_rsp0 = (uint64_t)kstack_top;   // 用户进程的独立内核栈顶
    p->is_user = 1;
    p->is_linux = is_linux;
    p->state = TS_READY;
    p->quantum = QUANTUM_TICKS;
    p->pml4_phys = pml4;                    // 调用方提供的独立页表
    p->entry = entry;                       // 记录入口 (SYS_SPAWN 用)
    p->user_rsp = user_rsp;                 // 记录用户栈顶 (SYS_SPAWN 用)
    int j; for (j = 0; j < 15 && name && name[j]; j++) p->name[j] = name[j];
    p->name[j] = 0;

    g_thread_count++;
    s_puts("[sched] created user process pid=");
    s_putd(p->pid);
    s_puts(" name=");
    s_puts(p->name);
    s_puts(" entry=0x");
    s_putd((int)entry);

    s_puts("\r\n");
    return p->pid;
}
// 标记当前线程终止, 永久挂起
// ============================================================
void sched_exit(void) {
    if (g_current) {
        g_current->state = TS_DEAD;
        s_puts("[sched] thread pid=");
        s_putd(g_current->pid);
        s_puts(" exited\r\n");
    }
    // 开中断并永久 hlt: 下个 tick 调度器选走别的线程, 不再选回 DEAD
    for (;;) { __asm__ volatile("sti; hlt"); }
}

// ============================================================
// pit_handler 调用: 标记需要调度检查
// ============================================================
void sched_request(void) {
    g_sched_pending = 1;
}

// ============================================================
// 选下一个非 DEAD 线程 (轮转)
// ============================================================
static pcb_t* pick_next(void) {
    if (g_thread_count <= 1) return g_current;
    int start = (int)(g_current - g_threads);
    for (int i = 1; i < g_thread_count; i++) {
        int idx = (start + i) % g_thread_count;
        if (g_threads[idx].state != TS_DEAD) return &g_threads[idx];
    }
    return g_current;  // 全死了就留在当前
}

// ============================================================
// isr_common 调用: 调度核心
// ============================================================
uint64_t sched_tick_isr(uint64_t cur_rsp) {
    if (!g_sched_inited || !g_sched_pending) return 0;
    g_sched_pending = 0;

    pcb_t* cur = g_current;
    cur->saved_rsp = cur_rsp;          // 保存当前栈帧

    if (cur->state == TS_DEAD) {
        // 当前已死, 直接切走
        pcb_t* next = pick_next();

        if (next == cur) return 0;     // 没有别的活线程
        g_current = next;
        next->state = TS_RUNNING;
        next->quantum = QUANTUM_TICKS;
        gdt_set_rsp0(next->kernel_rsp0);  // 切 TSS.rsp0 到新线程的内核栈
        syscall_set_kernel_rsp(next->kernel_rsp0);  // 切 syscall 栈 (阶段 4)
        if (next->pml4_phys) paging_switch(next->pml4_phys);  // 切页表 (阶段 4)
        return next->saved_rsp;
    }

    if (--cur->quantum > 0) return 0;  // 时间片未用完

    pcb_t* next = pick_next();
    if (next == cur) {
        cur->quantum = QUANTUM_TICKS;   // 只有自己, 续命
        return 0;
    }
    cur->state = TS_READY;
    next->state = TS_RUNNING;
    next->quantum = QUANTUM_TICKS;
    g_current = next;

    gdt_set_rsp0(next->kernel_rsp0);      // 切 TSS.rsp0 到新线程的内核栈
    syscall_set_kernel_rsp(next->kernel_rsp0);  // 切 syscall 栈 (阶段 4)
    if (next->pml4_phys) paging_switch(next->pml4_phys);  // 切页表 (阶段 4)
    return next->saved_rsp;
}

int sched_current_pid(void) {
    return g_current ? g_current->pid : -1;
}

int sched_current_is_linux(void) {
    return g_current ? g_current->is_linux : 0;
}

int sched_thread_count(void) {
    return g_thread_count;
}

int sched_is_dead(int pid) {
    if (pid < 0 || pid >= g_thread_count) return 1;
    return g_threads[pid].state == TS_DEAD;
}

uint64_t sched_current_entry(void) {
    return g_current ? g_current->entry : 0;
}

uint64_t sched_current_user_rsp(void) {
    return g_current ? g_current->user_rsp : 0;
}

// ============================================================
// 测试线程: 往串口输出, 验证多线程切换
// ============================================================
static void test_thread_a(void) {
    for (int n = 0; n < 8; n++) {
        s_puts("[A#");
        s_putd(sched_current_pid());
        s_puts("] tick ");
        s_putd(n);
        s_puts("\r\n");
        for (volatile int i = 0; i < 3000000; i++);
    }
    sched_exit();
}

static void test_thread_b(void) {
    for (int n = 0; n < 8; n++) {
        s_puts("[B#");
        s_putd(sched_current_pid());
        s_puts("] tick ");
        s_putd(n);
        s_puts("\r\n");
        for (volatile int i = 0; i < 3000000; i++);
    }
    sched_exit();
}

// 创建测试线程 (kernel.c 在 app_run 前调用)
void sched_run_test(void) {
    sched_create_thread(test_thread_a, "testA");
    sched_create_thread(test_thread_b, "testB");
    s_puts("[sched] test threads created, scheduler running\r\n");
}