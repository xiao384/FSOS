// lang.c - 嵌入式语言运行时框架 (惰性 / 按需启动)
//
// 设计目标(用户约束): 内嵌的 Python / C/C++ / Java 只有在有程序需要调用时才
// 启动, 其它时间不启动, 节省内存与 CPU。
//
// C/C++ 与 Java 解释器是【可加载模块】: 镜像存于磁盘专用 LBA, 仅当 lang_launch
// 请求时由 module.c 从盘读入预留高地址区执行, 运行完即清零释放 -> 空闲内核
// 不含任何解释器代码/数据 (真正零占用)。Python 经 MicroPython REPL 接入, 运行期
// 才占用资源, 退出即释放 (惰性)。
#include "lang.h"
#include "proc.h"
#include "vga.h"
#include "mp_entry.h"
#include "module.h"
#include <stdint.h>
#include <stddef.h>          // NULL

// Python 经 mp_fsos_run(MP_MODE_REPL) 接入 (见 mp_port 入口).
// 该入口不返回直到用户退出 REPL -> 运行期间才占用资源, 退出即释放 (惰性).
static int py_init(void) { return 0; }
static int py_run(const char* src, const char* proc_name) {
    (void)proc_name;
    if (src) return mp_fsos_run_str(src);   // 来自"开发"应用的 .py 文件
    mp_fsos_run_buffered();                 // T4.2: 交互式 Python (缓冲式, 不清屏)
    return 0;
}
static void py_shutdown(void) { /* REPL 退出即释放 */ }

// C/C++ 与 Java: 运行期经模块加载器从磁盘载入, 空闲零占用
static int mod_init(void)  { return 0; }       // 无常驻状态
static int c_run(const char* src, const char* proc_name) {
    return mod_load_run("C/C++", src, proc_name);
}
static int j_run(const char* src, const char* proc_name) {
    return mod_load_run("Java", src, proc_name);
}
static void mod_shutdown(void) { /* 运行完 module.c 已清零释放 */ }

static lang_runtime_t g_rt[LANG_MAX];
static int g_n = 0;

void lang_init(void) {
    g_n = 0;
    // 注册三个运行时, 但 C/C++/Java 不启动任何代码 (惰性 + 模块化)
    g_rt[g_n++] = (lang_runtime_t){
        "Python", "MicroPython 解释器", 0, -1, py_init, py_run, py_shutdown
    };
    g_rt[g_n++] = (lang_runtime_t){
        "C/C++",  "C/C++ 25 (可加载模块)", 0, -1, mod_init, c_run, mod_shutdown
    };
    g_rt[g_n++] = (lang_runtime_t){
        "Java",   "Java 26 SE (可加载模块)", 0, -1, mod_init, j_run, mod_shutdown
    };
}

lang_runtime_t* lang_find(const char* name) {
    for (int i = 0; i < g_n; i++)
        if (__builtin_strcmp(g_rt[i].name, name) == 0) return &g_rt[i];
    return NULL;
}

int lang_count(void) { return g_n; }
lang_runtime_t* lang_get(int i) { return (i >= 0 && i < g_n) ? &g_rt[i] : NULL; }

int lang_is_active(const char* name) {
    lang_runtime_t* r = lang_find(name);
    return (r && r->active_idx >= 0) ? 1 : 0;
}

// 惰性启动: 首次调用时 init(); 登记程序进程; 执行 (模块从盘载入运行后释放); 退出释放登记。
// 返回 0 成功, 负值失败。
int lang_launch(const char* name, const char* src, const char* proc_name) {
    lang_runtime_t* r = lang_find(name);
    if (!r) return -1;

    if (r->initialized == 0) {
        int rc = r->init ? r->init() : 0;
        if (rc != 0) return -2;
        r->initialized = 1;
    }

    // 登记程序进程 (内存统计上升); 估算解释器内存占用 ~384KB
    int idx = proc_add(proc_name ? proc_name : r->name, r->name, 384);
    r->active_idx = idx;

    int rc = r->run ? r->run(src, proc_name) : 0;

    if (idx >= 0) proc_kill(idx);
    r->active_idx = -1;
    return rc;
}

void lang_shutdown_all(void) {
    for (int i = 0; i < g_n; i++) {
        if (g_rt[i].initialized && g_rt[i].shutdown) g_rt[i].shutdown();
        g_rt[i].initialized = 0;
        g_rt[i].active_idx = -1;
    }
}
