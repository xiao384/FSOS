// desktop.cpp - C++ 图形化入口 (已统一到新 WM)
//
// 历史: 此前这是一个独立的 C++ 按钮式桌面 (列出 Python/C/C++/Java 等运行时,
// 点击后惰性拉起对应语言运行时)。在 task40 "统一 app.c / desktop.cpp 入口到
// 新 WM" 改造后, 它不再维护自己的主循环, 而是直接委托给统一的 Windows 式窗口
// 管理器 wm_demo_run()。于是:
//   - app.c 的 C 侧 (按键 G) 与 desktop.cpp 的 C++ 侧 (按键 D) 共用同一个桌面
//   - 语言运行时入口已内建进 WM (开始菜单 + 桌面图标), 不再需要单独桌面
//
// 本文件仍以 freestanding C++ (-fno-rtti -fno-exceptions -nostdlib) 编译,
// 作为 C++ 工具链仍可链接进内核的验证点; 其行为现完全等同于进入 WM。
#include <cstddef>

// wm_demo_run 由 C 侧 wm.c 实现, 必须 extern "C" 链接。
extern "C" {
#include "wm.h"
}

// C++ 图形化入口: 统一委托给新 WM (语言运行时桌面已并入其中)。
extern "C" void desktop_run(void) {
    wm_demo_run();
}
