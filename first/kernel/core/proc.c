// proc.c - 进程注册表实现
#include "proc.h"
#include "kheap.h"
#include <stdint.h>

#define PROC_MAX 24

static proc_t g_procs[PROC_MAX];
static int    g_count = 0;
static int    g_inited = 0;

// 伪随机游走种子 (用滴答扰动, 不必精确)
static uint32_t s_lcg = 0x12345678u;
static uint32_t lcg_next(void) {
    s_lcg = s_lcg * 1664525u + 1013904223u;
    return s_lcg;
}

static void add_proc(int pid, const char* name, const char* desc,
                     proc_type_t t, uint32_t mem_kb, int killable) {
    if (g_count >= PROC_MAX) return;
    proc_t* p = &g_procs[g_count++];
    p->pid = pid;
    p->name = name;
    p->desc = desc;
    p->type = t;
    p->state = PROC_RUNNING;
    p->mem_kb = mem_kb;
    p->cpu_permille = (t == PROC_SYS_NECESSARY) ? 20 + (int)(lcg_next() % 30)
                                                : 10 + (int)(lcg_next() % 120);
    p->cpu_time_ms = (uint64_t)(lcg_next() % 4000);
    p->killable = killable;
    p->owned = NULL;
    p->owned_size = 0;
}

void proc_init(void) {
    if (g_inited) return;
    g_inited = 1;
    g_count = 0;

    // ---- 系统必要进程 (不可结束) ----
    add_proc(1,  "kernel",  "内核核心 (调度/内存/中断)", PROC_SYS_NECESSARY, 1024, 0);
    add_proc(2,  "idt",     "中断与异常分发",           PROC_SYS_NECESSARY, 32,   0);
    add_proc(3,  "pit",     "系统定时器 1000Hz",        PROC_SYS_NECESSARY, 8,    0);
    add_proc(4,  "vga",     "显示驱动 (VGA/VBE)",       PROC_SYS_NECESSARY, 64,   0);
    add_proc(5,  "kb",      "键盘驱动",                 PROC_SYS_NECESSARY, 16,   0);
    add_proc(6,  "mouse",   "鼠标驱动",                 PROC_SYS_NECESSARY, 24,   0);
    add_proc(7,  "ata",     "磁盘驱动",                 PROC_SYS_NECESSARY, 128,  0);
    add_proc(8,  "wm",      "窗口管理器",               PROC_SYS_NECESSARY, 256,  0);
    // 注: python 运行时改为按需启动(见 lang.c), 不在启动时常驻, 空闲不占内存。

    // ---- 系统非必要进程 (可结束) ----
    add_proc(10, "logind",  "登录服务",                 PROC_SYS_OPTIONAL, 64,   1);
    add_proc(11, "usrmgrd", "用户管理服务",             PROC_SYS_OPTIONAL, 96,   1);
    add_proc(12, "termd",   "终端守护进程",             PROC_SYS_OPTIONAL, 48,   1);

    // ---- 程序进程 (可结束, 拥有真实内核堆缓冲) ----
    // 分配内核堆缓冲, 结束时 kfree 释放, 内存条会真实下降
    struct { int pid; const char* name; const char* desc; uint32_t kb; } prog[] = {
        { 13, "terminal", "终端程序",        256 },
        { 14, "desktop",  "图形桌面程序",    512 },
        { 15, "usrapp",   "用户管理程序",    128 },
        { 16, "taskmgr",  "任务管理器 (本程序)", 128 },
    };
    for (unsigned i = 0; i < sizeof(prog) / sizeof(prog[0]); i++) {
        add_proc(prog[i].pid, prog[i].name, prog[i].desc, PROC_PROGRAM, prog[i].kb, 1);
        // 在进程表末尾找到刚加入的项, 分配其专属缓冲
        proc_t* p = &g_procs[g_count - 1];
        uint32_t bytes = prog[i].kb * 1024u;
        p->owned = kmalloc(bytes);
        p->owned_size = p->owned ? bytes : 0;
        if (!p->owned) p->mem_kb = 0;
    }
}

int proc_count(void) { return g_count; }
proc_t* proc_get(int i) {
    if (i < 0 || i >= g_count) return NULL;
    return &g_procs[i];
}

int proc_kill(int i) {
    proc_t* p = proc_get(i);
    if (!p) return -2;
    if (!p->killable) return -1;                 // 系统必要进程, 不可结束
    if (p->state != PROC_RUNNING) return -2;     // 已结束/挂起
    // 释放程序进程拥有的内核堆缓冲 (真实内存回收)
    if (p->owned) {
        kfree(p->owned);
        p->owned = NULL;
        p->owned_size = 0;
        p->mem_kb = 0;
    }
    p->state = PROC_TERMINATED;
    p->cpu_permille = 0;
    return 0;
}

// 动态添加程序进程 (语言运行时按需启动时调用).
// 分配内核堆缓冲使内存统计真实上升; 返回进程表索引 (供 proc_kill 释放), -1 失败.
int proc_add(const char* name, const char* desc, uint32_t mem_kb) {
    if (g_count >= PROC_MAX) return -1;
    int pid = 100 + g_count;   // 与静态 pid 区分
    add_proc(pid, name, desc, PROC_PROGRAM, mem_kb, 1);
    proc_t* p = &g_procs[g_count - 1];
    uint32_t bytes = mem_kb * 1024u;
    p->owned = kmalloc(bytes);
    p->owned_size = p->owned ? bytes : 0;
    if (!p->owned) p->mem_kb = 0;
    p->state = PROC_RUNNING;
    return g_count - 1;        // 索引
}

int proc_resume(int i) {
    proc_t* p = proc_get(i);
    if (!p) return -2;
    if (!p->killable) return -1;
    if (p->state == PROC_RUNNING) return 0;
    // 程序进程若被结束且缓冲已释放, 无法真正"复活", 这里仅作演示复位状态
    p->state = PROC_RUNNING;
    if (!p->owned && p->owned_size == 0) {
        // 重新分配 (若之前被释放)
        p->owned = kmalloc(128 * 1024);
        p->owned_size = p->owned ? 128u * 1024u : 0;
        p->mem_kb = p->owned ? 128 : 0;
    }
    return 0;
}

void proc_tick(uint32_t dt_ms) {
    for (int i = 0; i < g_count; i++) {
        proc_t* p = &g_procs[i];
        if (p->state != PROC_RUNNING) continue;
        // 随机游走, 维持大致合理范围
        int c = p->cpu_permille;
        c += (int)(lcg_next() % 21) - 10;        // ±10
        if (c < 0) c = 0;
        if (c > 990) c = 990;
        p->cpu_permille = c;
        p->cpu_time_ms += (uint64_t)((uint64_t)c * dt_ms / 1000u);
    }
}

const char* proc_type_str(proc_type_t t) {
    switch (t) {
        case PROC_SYS_NECESSARY: return "SYS ";
        case PROC_SYS_OPTIONAL:  return "OPT ";
        default:                 return "APP ";
    }
}
const char* proc_state_str(proc_state_t s) {
    switch (s) {
        case PROC_RUNNING:    return "running";
        case PROC_SUSPENDED:  return "suspend";
        default:              return "ended  ";
    }
}
