// mp_stub.c - 未启用 MicroPython 时的占位实现
// 让内核在没有构建 MicroPython 的情况下仍能编译/运行,
// 终端输入 python / bt 时给出友好提示。
#include "mp_entry.h"
#include "vga.h"

// 桩实现: 本内核没有真正的 MicroPython, 终端应回退到内置 C 终端
int mp_available(void) {
    return 0;
}

int mp_fsos_run(int mode) {
    (void)mode;
    vga_clear(COL_BLACK);
    vga_draw_text(4, 80, "[python] MicroPython is not built into this kernel.",
                  COL_LRED, COL_BLACK);
    vga_draw_text(4, 92, "Rebuild with the MicroPython switch to enable it.",
                  COL_LGRAY, COL_BLACK);
    // 等待任意键返回
    extern int kb_wait(void);
    kb_wait();
    return 0;   // 占位构建无 MP -> 调用方回退到内置 C 终端
}

// 占位构建无 MicroPython, 预编译为空操作 (链接兼容)
void mp_fsos_prefetch(void) {}

// 占位构建不能执行 Python 源码
int mp_fsos_run_str(const char* src) {
    (void)src;
    vga_draw_text(4, 80, "[python] MicroPython is not built into this kernel.",
                  COL_LRED, COL_BLACK);
    vga_draw_text(4, 92, "Rebuild with the MicroPython switch to enable it.",
                  COL_LGRAY, COL_BLACK);
    extern int kb_wait(void);
    kb_wait();
    return -1;
}
