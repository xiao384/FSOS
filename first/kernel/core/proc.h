// proc.h - 进程注册表 (任务管理器展示用的"逻辑进程"模型)
//
// 注意: FSOS 已具备真正的抢占式调度器 (见 sched.c: PIT 时钟 + 上下文切换,
// 支持内核线程与 ring3 用户进程, 包括 Linuxulator 的 is_linux 进程)。
// 本文件不是调度器, 而是一张供任务管理器展示用的"逻辑进程"表: 把
// "系统必要进程 / 系统非必要进程 / 程序进程"三类以 proc_t 呈现,
// 与 sched.c 的 pcb_t 是两套独立数据结构。
// 程序进程 (PROC_PROGRAM) 拥有真实的内核堆缓冲, 结束时通过 kfree 释放,
// 内存占用 (kheap_used) 会真实下降, 使"结束进程"可见。
#ifndef PROC_H
#define PROC_H

#include <stdint.h>

typedef enum {
    PROC_SYS_NECESSARY,  // 系统必要进程 (不可结束)
    PROC_SYS_OPTIONAL,   // 系统非必要进程 (可结束)
    PROC_PROGRAM         // 用户/程序进程 (可结束, 拥有内核堆缓冲)
} proc_type_t;

typedef enum {
    PROC_RUNNING,
    PROC_SUSPENDED,
    PROC_TERMINATED
} proc_state_t;

typedef struct {
    int         pid;
    const char* name;
    const char* desc;
    proc_type_t type;
    proc_state_t state;
    uint32_t    mem_kb;        // 当前内存占用 (KB). 程序进程结束后归 0
    int         cpu_permille;  // 实时 CPU 占用 (千分之一, 模拟动态)
    uint64_t    cpu_time_ms;   // 累计 CPU 时间 (ms, 模拟)
    int         killable;      // 1=可结束 (系统必要进程为 0)
    void*       owned;         // 程序进程拥有的内核堆缓冲 (结束即释放)
    uint32_t    owned_size;    // owned 缓冲字节数
} proc_t;

// 初始化进程表 (内含程序进程的内核堆分配)
void   proc_init(void);
int    proc_count(void);
proc_t* proc_get(int i);
// 结束进程: 0=成功, -1=系统必要不可结束, -2=已结束/挂起无效
int    proc_kill(int i);
// 恢复已结束/挂起的进程 (仅对可结束进程有意义)
int    proc_resume(int i);
// 每个采样周期推进一次 (累计 cpu_time, 随机游走 cpu_permille)
void   proc_tick(uint32_t dt_ms);

const char* proc_type_str(proc_type_t t);
const char* proc_state_str(proc_state_t s);

// 动态添加程序进程 (语言运行时按需启动时调用, 体现"空闲不占用"):
// 分配内核堆缓冲(内存统计真实上升), 返回进程表索引 i (供 proc_kill 释放), -1 失败
int proc_add(const char* name, const char* desc, uint32_t mem_kb);

#endif // PROC_H
