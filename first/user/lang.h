// lang.h - 嵌入式语言运行时框架 (惰性 / 按需启动)
//
// 设计目标(用户约束): 内嵌的 Python / C/C++ / Java 只有在有程序需要调用时才
// 启动, 其它时间不启动, 节省内存与 CPU。
//
// 每个语言是一个 lang_runtime_t: init() 仅首次 lang_launch 时调用一次,
// 之后常驻(已初始化状态可复用); run() 真正执行用户程序; shutdown() 释放资源。
// 启动程序时经 proc_add 向任务管理器的"程序进程"栏登记, 退出时 proc_kill
// 释放其内核堆 → 内存统计真实回落, 直观体现"空闲不占用"。
#ifndef LANG_H
#define LANG_H

#include <stdint.h>

#define LANG_MAX 8

typedef struct lang_runtime {
    const char* name;          // "Python" / "C/C++" / "Java"
    const char* blurb;         // 桌面按钮副标题
    int         initialized;    // 框架置位: init 是否已执行
    int         active_idx;     // 当前运行程序的进程表索引, -1=无
    int       (*init)(void);                       // 返回 0 成功
    int       (*run)(const char* src, const char* proc_name); // 执行程序
    void      (*shutdown)(void);
} lang_runtime_t;

void lang_init(void);                              // 注册运行时(不启动)
lang_runtime_t* lang_find(const char* name);
int  lang_count(void);
lang_runtime_t* lang_get(int i);

// 惰性启动: 首次调用时 init(); 登记程序进程; 执行; 退出后释放。
// 返回 0 成功, 负值失败。
int  lang_launch(const char* name, const char* src, const char* proc_name);
void lang_shutdown_all(void);
int  lang_is_active(const char* name);

#endif // LANG_H
