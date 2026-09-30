// mp_entry.c - FSOS 上的 MicroPython 运行时入口
// 由内核 terminal.c 的 mp_fsos_run() 调用.
#include <stdint.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "shared/runtime/pyexec.h"

#include "vga.h"
#include "mp_entry.h"
#include "filesys.h"

// T4.1: HAL 缓冲输出开关 (mphalport.c)
extern void mp_hal_set_buffered(int on);

// 内嵌的 "Better terminal" Python 源码 (由 gen_frozen_fsos.c 提供)
extern const char bt_py_source[];

// 内核栈顶符号 (start.asm 中定义), 用于 GC 栈扫描
extern char stack_top[];

// 执行一段 Python 源码字符串
static void do_str(const char *src, mp_parse_input_kind_t input_kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

// 自定义 REPL: 行为同 MicroPython 友好 REPL, 但额外支持 exit / quit / Ctrl+D 退出,
// 并回显输入字符 (内置 pyexec_friendly_repl 仅认 Ctrl+D, 且输入不回显时易误以为"卡死")。
static void fsos_repl(void) {
    static const char banner[] = "\r\nFSOS MicroPython (键入 exit / quit 或 Ctrl+D 退出)\r\n";
    mp_hal_stdout_tx_strn(banner, (uint32_t)sizeof(banner) - 1);
    char line[256];
    for (;;) {
        mp_hal_stdout_tx_strn(">>> ", 4);
        int n = 0, esc = 0;
        for (;;) {
            int c = mp_hal_stdin_rx_chr();
            if (esc) {                                   // 丢弃转义序列 (方向键/Home/End 等)
                if ((c >= 'A' && c <= 'Z') || c == '~') esc = 0;
                else if (c != '[') esc = 0;
                continue;
            }
            if (c == 27) { esc = 1; continue; }          // ESC: 进入转义序列
            if (c == 4)  { mp_hal_stdout_tx_strn("\r\n", 2); return; }  // Ctrl+D = EOF
            if (c == '\r' || c == '\n') { mp_hal_stdout_tx_strn("\r\n", 2); line[n] = 0; break; }
            if (c == 8 || c == 127) {                   // 退格/删除: 本地缓冲回退并回显擦除
                if (n > 0) { n--; mp_hal_stdout_tx_strn("\b \b", 3); }
                continue;
            }
            if (c >= 32 && c < 127 && n < (int)sizeof(line) - 1) {
                line[n++] = (char)c;
                char ch = (char)c;
                mp_hal_stdout_tx_strn(&ch, 1);          // 回显输入字符
            }
        }
        if (n == 0) continue;
        if (n == 4 && (strncmp(line, "exit", 4) == 0 || strncmp(line, "quit", 4) == 0)) return;
        do_str(line, MP_PARSE_SINGLE_INPUT);
    }
}

// ---- 运行时持久化 ----
// 过去 mp_fsos_run 每次进入都把整段 Better terminal 源码重新解析+编译一遍,
// 在模拟 CPU 上表现为"打开终端黑屏卡一下"。现改为: 运行时与编译好的字节码只创建一次,
// 之后重复打开终端直接复用已编译代码, 卡顿只会在首次(且带 Loading 提示)出现一次。
static int      g_mp_ready = 0;

static mp_obj_t g_bt_fun = NULL;
#define G_BT_FUN g_bt_fun

// 把内嵌 Better terminal 源码编译成字节码 (仅首次). 失败返回 NULL, 由调用方回退打印错误.
static mp_obj_t compile_bt_once(void) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, bt_py_source,
                                                    strlen(bt_py_source), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t fun = mp_compile(&parse_tree, source_name, true);
        nlr_pop();
        return fun;
    } else {
        mp_obj_t exc = (mp_obj_t)nlr.ret_val;
        mp_obj_print_exception(&mp_plat_print, exc);
        // SyntaxError 的 value(args) 含 (msg, (file,line,col,text)), 打印出来便于定位
        mp_printf(&mp_plat_print, "EXC_ARGS:");
        mp_obj_print_helper(&mp_plat_print, mp_obj_exception_get_value(exc), PRINT_REPR);
        mp_printf(&mp_plat_print, "\r\n");
        return NULL;
    }
}

// 运行 Better terminal; 自带异常捕获, 保证 BT 内未处理异常不会击穿到内核
static void run_bt(void) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        if (!G_BT_FUN) {
            // 仅在真正需要编译时显示提示, 避免大段源码编译期间黑屏被误认成"卡死"
            vga_clear(COL_BLACK);
            vga_draw_text_center(96, "Loading terminal...", COL_LGRAY, COL_BLACK);
            G_BT_FUN = compile_bt_once();
        }
        if (G_BT_FUN) mp_call_function_0(G_BT_FUN);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

#if MICROPY_ENABLE_GC
void gc_collect(void) {
    // 扫描 C 栈: 从当前栈变量到内核栈顶.
    // (MicroPython 会自动把 mp_state_ctx 作为根, 无需手动扫描)
    //
    // 说明: 扫描整段内核栈(64KB)会把栈上的非指针垃圾误判为 GC 指针,
    // 但这些值几乎不可能落在 GC 堆(0x04000000-0x04800000)范围内,
    // 因此 gc_mark_subtree 会安全忽略它们; 若真要精确, 可在此过滤范围。
    void *dummy;
    gc_collect_start();
    // Better Terminal 的已编译函数对象位于 GC 堆，但由 C 全局变量持有；
    // 全局变量不在 MicroPython 栈扫描范围内，因此显式注册为 GC root。
    gc_collect_root(&g_bt_fun, 1);
    // 用 uintptr_t 计算栈范围 (64 位下 uint32_t 会截断指针)
    gc_collect_root(&dummy, ((uintptr_t)stack_top - (uintptr_t)&dummy) / sizeof(uintptr_t));
    gc_collect_end();
}
#endif

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *path = qstr_str(filename);
    char *buf = m_malloc(FS_MAX_SIZE + 1);
    if (!buf) mp_raise_OSError(MP_ENOMEM);
    int n = fs_read(path, buf, FS_MAX_SIZE + 1);
    if (n < 0 && path[0] == '.' && path[1] == '/') {
        m_free(buf);
        path += 2;
        buf = m_malloc(FS_MAX_SIZE + 1);
        if (!buf) mp_raise_OSError(MP_ENOMEM);
        n = fs_read(path, buf, FS_MAX_SIZE + 1);
    }
    if (n < 0) {
        m_free(buf);
        mp_raise_OSError(MP_ENOENT);
    }
    return mp_lexer_new_from_str_len(filename, buf, (size_t)n, FS_MAX_SIZE + 1);
}

mp_import_stat_t mp_import_stat(const char *path) {
    if (fs_size(path) >= 0) return MP_IMPORT_STAT_FILE;
    if (path[0] == '.' && path[1] == '/' && fs_size(path + 2) >= 0)
        return MP_IMPORT_STAT_FILE;
    return MP_IMPORT_STAT_NO_EXIST;
}

void nlr_jump_fail(void *val) {
    (void)val;
    for (;;) { }
}

void NORETURN __fatal_error(const char *msg) {
    (void)msg;
    for (;;) { }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)file; (void)line; (void)func; (void)expr;
    for (;;) { }
}
#endif

// 本内核编译进了真正的 MicroPython 运行时 -> 终端交给 Better terminal
int mp_available(void) {
    return 1;
}

// 执行一段 Python 源码（桌面“开发”应用运行 .py 文件时调用）。
// 与 REPL 的区别：执行完源码立即返回；stdout 同时进入 FSOS console buffer，
// 由 DevStudio/Terminal 的输出面板显示，绝不清屏，也不等待“Press any key”。
int mp_fsos_run_str(const char *src) {
    if (!src) return -1;
    if (!g_mp_ready) {
        nlr_buf_t nlr;
        mp_hal_console_reset();
        if (nlr_push(&nlr) == 0) {
            gc_init(MP_HEAP_START, (char *)MP_HEAP_START + MP_HEAP_SIZE);
            mp_init();
            g_mp_ready = 1;
            nlr_pop();
        } else {
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
            return -1;
        }
    } else {
        mp_hal_console_reset();
    }
    do_str(src, MP_PARSE_FILE_INPUT);
    return 0;
}

// 桌面启动期调用: 初始化运行时并预编译 BT 字节码, 使后续打开终端瞬时 (不再现场编译卡顿)。
// 可安全多次调用: 运行时只初始化一次, 字节码只编译一次。占位构建下此函数不被链接。
//
// 注意: 本函数在 wm_demo_run() 首帧绘制之前调用, 若在它内部崩溃, 串口日志仍会停在
// "FSOS_BOOT_OK" 之前, 画面表现为黑屏/卡死, 易被误判为"显示有问题"。
// 因此必须:
//   1) 先画一张 "Loading desktop..." 提示, 让等待可预期;
//   2) 用 nlr 把运行时初始化包起来, 失败也不击穿内核 —— 桌面照常启动,
//      打开终端时由 run_bt() 现场编译 (带 "Loading terminal..." 提示)。
static int g_bt_tried = 0;   // 预编译尝试过(含失败): 桌面期避免每帧重复编译风暴

void mp_fsos_prefetch(void) {
    if (G_BT_FUN || g_bt_tried) return;         // 已编译或已尝试过(失败), 直接返回
    g_bt_tried = 1;

    // 首帧之前的等待提示, 避免黑屏被误认为显示异常
    vga_clear(COL_BLACK);
    vga_draw_text_center(96, "Loading desktop...", COL_LGRAY, COL_BLACK);

    if (!g_mp_ready) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            gc_init(MP_HEAP_START, (char *)MP_HEAP_START + MP_HEAP_SIZE);
            mp_init();
            g_mp_ready = 1;
            nlr_pop();
        } else {
            // 运行时初始化失败: 不致命, 桌面照常启动, 打开终端时回退到现场编译
            g_mp_ready = 0;
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
            return;
        }
    }
    G_BT_FUN = compile_bt_once();               // 编译失败返回 NULL, 打开终端时回退到带提示的现场编译
}

// 运行时入口: mode = MP_MODE_REPL 或 MP_MODE_BT
// 返回 1 = 已正常运行; 返回 0 = 该模式未能启动 (BT 编译失败等), 调用方回退。
int mp_fsos_run(int mode) {
    if (!g_mp_ready) {
        // 首次进入: 初始化运行时 (仅一次)。初始化失败时回退到 C 终端，
        // 不允许 MicroPython 的异常直接击穿内核。
        nlr_buf_t nlr;

        mp_hal_console_reset();
        if (nlr_push(&nlr) == 0) {
            gc_init(MP_HEAP_START, (char *)MP_HEAP_START + MP_HEAP_SIZE);
            mp_init();
            g_mp_ready = 1;
            nlr_pop();
        } else {
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
            return 0;
        }
    } else {
        mp_hal_console_reset();
    }

    if (mode == MP_MODE_BT) {
        // run_bt 在编译失败时不会调用 G_BT_FUN; 返回是否真正进入了 BT。
        int ok = G_BT_FUN ? 1 : 0;
        run_bt();
        return G_BT_FUN ? 1 : 0;
    } else {
#if MICROPY_ENABLE_COMPILER
        // T4.1: REPL 已移至 mp_fsos_run_buffered, 不再清屏/直写全屏文本
        return 1;
#else
        run_bt();
        return G_BT_FUN ? 1 : 0;
#endif
    }

    // 不再调用 mp_deinit(): 保留 MicroPython 运行时与已编译字节码,
    // 使后续重复打开终端无需重新初始化/编译, 彻底消除"每次开终端卡一下"。
    // (MP 堆为专用 8MB, 不会被内核其它部分复用, 常驻安全)
}
// T4.1: 缓冲式交互 REPL — stdout 经 console_emit 进入环形缓冲 (module.c),
// 由终端/DevStudio 输出面板 console_drain 回填, 不直写 VGA 全屏文本。
int mp_fsos_run_buffered(void) {
    if (!g_mp_ready) {
        nlr_buf_t nlr;
        mp_hal_console_reset();
        if (nlr_push(&nlr) == 0) {
            gc_init(MP_HEAP_START, (char *)MP_HEAP_START + MP_HEAP_SIZE);
            mp_init();
            g_mp_ready = 1;
            nlr_pop();
        } else {
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
            return 0;
        }
    } else {
        mp_hal_console_reset();
    }
#if MICROPY_ENABLE_COMPILER
    mp_hal_set_buffered(1);
    fsos_repl();
    mp_hal_set_buffered(0);
    return 1;
#else
    run_bt();
    return G_BT_FUN ? 1 : 0;
#endif
}
