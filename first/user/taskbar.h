// taskbar.h - FSOS 任务栏 (gui_framework Phase 5)
//
// 从 wm.c 拆分: 任务栏绘制 + RTC 时钟 + 运行中应用列表。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.6
#ifndef FSOS_TASKBAR_H
#define FSOS_TASKBAR_H

#include "window.h"    // WM_TASKBAR_Y, NAPP, app_t

// ==================== 任务栏接口 ====================

void taskbar_init(void);
void taskbar_draw(int mx, int my);

// 返回任务栏固定入口命中的动作；-1 表示未命中。
int taskbar_hit_action(int mx, int my);

// 获取任务栏矩形区域 (供 workarea 计算)
void taskbar_get_rect(int* x, int* y, int* w, int* h);

// ==================== RTC 接口 (从 wm.c 公开) ====================

void rtc_update(void);

extern int rtc_h, rtc_m, rtc_s, rtc_ok;
extern int rtc_y, rtc_mo, rtc_d;

// ==================== 状态 (定义在 wm.c, extern 供 taskbar.c) ====================

extern int g_start_open;

#endif // FSOS_TASKBAR_H