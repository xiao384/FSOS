// shell.h - FSOS 桌面外壳 (gui_framework Phase 4)
//
// 从 wm.c 拆分: 壁纸/图标/右键菜单/帮助覆盖/新手提示/电源操作。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.4
// 注意: 文件名 shell.c 避免与已有 desktop.h/cpp (C++ 启动器) 冲突。
#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include "window.h"    // WM_TASKBAR_Y, NAPP, ACT_*
#include "sidebar.h"   // 控制中心 (顶栏箭头状态)

// ==================== 桌面绘制 ====================

// draw_icon 定义在 wm.c, 公开供 shell.c 使用
void draw_icon(int kind, int x, int y);
void draw_icon_big(int kind, int x, int y, int size);

void desktop_draw_wallpaper(void);
void desktop_draw_topbar(void);
int  desktop_topbar_h(void);            // 顶栏高度 (sidebar 对齐热区用)
int  desktop_topbar_cluster_x(void);    // 顶栏右侧状态簇左边界 (=控制中心热区)
void desktop_draw_icons(int mx, int my);
int  desktop_icon_hit(int mx, int my);
void desktop_draw_tip(void);
void desktop_draw_context_menu(int mx, int my);
void desktop_draw_help_overlay(void);

// ==================== 右键菜单 ====================

int  desktop_ctx_hit(int mx, int my);
void desktop_ctx_execute(int idx);

// ==================== 电源操作 ====================

void desktop_poweroff(void);
void desktop_reboot(void);

// ==================== 桌面状态 (定义在 wm.c, extern 供 shell.c) ====================

extern int g_sel_icon;
extern int g_ctx_open;
extern int g_ctx_x, g_ctx_y;
extern int g_force_redraw;
extern int g_tip_visible;
extern uint32_t g_tip_until;
extern int g_show_help;
extern int g_wallpaper_palette_ready;

// 桌面图标数据 (定义在 wm.c)
typedef struct { const char* label; int act; int kind; } icon_entry_t;
extern const icon_entry_t g_icons[];
extern const int g_nicon;

// 右键菜单数据 (定义在 wm.c)
typedef struct { const char* label; int act; } ctx_entry_t;
extern const ctx_entry_t g_ctx[];
extern const int g_nctx;

// 热键表 (定义在 wm.c, 供 help overlay 读取)
typedef struct {
    int          mods;
    int          key;
    const char*  label;
    void       (*fn)(void);
} hotkey_t;
extern const hotkey_t g_hotkeys[];
extern const int g_nhk;

#endif // SHELL_H