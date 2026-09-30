// startmenu.h - FSOS 开始菜单 (gui_framework Phase 6)
//
// 从 wm.c 拆分: 开始菜单绘制 + 网格布局 + 命中测试。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.7
#ifndef STARTMENU_H
#define STARTMENU_H

#include "window.h"    // WM_TASKBAR_Y, ACT_*

// ==================== 开始菜单接口 ====================

void startmenu_draw(int mx, int my);
void startmenu_set_open(int open);
int  startmenu_width(void);
int  startmenu_height(void);
int  startmenu_x0(void);
int  startmenu_y0(void);
void startmenu_item_rect(int i, int* rx, int* ry, int* rw, int* rh);
// 底部用户/电源按钮命中：返回 ACT_LOGOFF / ACT_POWEROFF / 0。
int startmenu_footer_hit(int mx, int my);

// ==================== 开始菜单数据 (定义在 wm.c) ====================

typedef struct { const char* label; int act; int kind; } startmenu_entry_t;
extern const startmenu_entry_t g_start[];
extern const int g_nstart;

#endif // STARTMENU_H