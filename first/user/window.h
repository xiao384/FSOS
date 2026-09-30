// window.h - FSOS 窗口管理器 (gui_framework Phase 3)
//
// 从 wm.c 拆分窗口状态机 + 生命周期 + 绘制 + 命中测试。
// app_t 回调契约保持不变 (draw/on_key/on_mouse/on_tick/on_rmouse)。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.3
#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include "vga.h"       // VGA_W, SCREEN_H, COL_*
#include "theme.h"     // THEME_SF, RADIUS_*
#include "animation.h" // T3.4: 窗口动画 (anim_id_t / anim_type_t)

// ---- 布局常量 (从 wm.c 迁移, 供 wm.c 与 window.c 共用) ----
#define WM_TASKBAR_H  ((SCREEN_H) >= 700 ? 84 : ((SCREEN_H) >= 420 ? 64 : 24))
#define WM_TASKBAR_Y  (SCREEN_H - WM_TASKBAR_H)
#define WM_TITLE_H    ((SCREEN_H) >= 700 ? 52 : ((SCREEN_H) >= 420 ? 42 : 20))

#define MAXAPP 9
#define NAPP   9

// ---- 特殊启动动作 ----
#define ACT_TERMINAL  (-2)
#define ACT_LOGOFF    (-3)
#define ACT_PYTHON    (-4)
#define ACT_CC        (-5)
#define ACT_JAVA      (-6)
#define ACT_POWEROFF  (-7)
#define ACT_START     (-8)

// app_t::on_key 返回值：1=消费事件，0=未消费，2=请求窗口管理器关闭当前应用。
#define GUI_KEY_CLOSE 2

// ---- 应用窗口结构体 (app_t 契约, 签名不变) ----
typedef struct {
    const char* name;
    int        x, y, w, h;
    int        open;
    int        minimized;
    int        maximized;
    int        ox, oy, ow, oh;   // 最大化前原几何
    int        z;
    void (*draw)(int x, int y, int w, int h);
    int  (*on_key)(int k);
    int  (*on_mouse)(int x, int y, int ldown);
    int  (*on_tick)(void);
    int  (*on_rmouse)(int mx, int my);
} app_t;

// ---- 命中类型 ----
typedef enum {
    HT_NONE   = -1,
    HT_CLOSE  = 0,
    HT_MAX    = 1,
    HT_MIN    = 2,
    HT_TITLE  = 3,
    HT_CLIENT = 4,
} hit_kind_t;

// ==================== 全局状态访问器 ====================
// g_maxz, g_focus, g_drag 从 wm.c 迁移至 window.c 内部静态

int  wm_get_focus(void);
void wm_set_focus(int idx);
int  wm_get_maxz(void);
void wm_set_maxz(int z);
int  wm_next_z(void);           // ++g_maxz, 返回新值
int  wm_get_drag(void);
void wm_set_drag(int idx);
int  wm_get_dox(void);
int  wm_get_doy(void);
void wm_set_drag_offset(int dox, int doy);

// ==================== 窗口绘制与命中测试 ====================

// 绘制单个窗口 (focused=1 聚焦样式)
void wm_draw_window(app_t* a, int focused);

// 命中窗口子区域. *what: HT_CLOSE/HT_MAX/HT_MIN/HT_TITLE/HT_CLIENT
// 返回 1 命中, 0 未命中 (*what=HT_NONE)
int  wm_hit_test(app_t* a, int mx, int my, int* what);

// 圆角矩形命中判定 (供其他模块复用)
int  wm_round_rect_hit(int mx, int my, int x, int y, int w, int h, int r);

// 窗口按钮 x 坐标 (btn: 0=close, 1=max, 2=min)
int  wm_win_btn_x(app_t* a, int btn);

// ==================== 窗口动画状态机 (T3.4/T3.5) ====================

// 开始窗口几何动画 (记录起止几何, 由 anim_create 驱动)。
// 返回 anim_id; ANIM_INVALID 表示动画禁用或池满, 调用方应即时切换几何。
anim_id_t wm_window_anim_begin(app_t* a, anim_type_t type, int to_x, int to_y, int to_w, int to_h);

// 获取窗口当前帧几何 (动画中为插值几何, 否则返回 a->x/y/w/h)
void wm_window_anim_get_geom(app_t* a, int* x, int* y, int* w, int* h);

// 查询窗口是否有活跃动画
int  wm_window_anim_active(app_t* a);

// 每帧推进: 动画完成时执行待定动作 (最小化), 并清除动画标记
void wm_window_anim_tick(void);

// ==================== g_app 数组 (定义在 wm.c, extern 供 window.c 访问) ====================
extern app_t g_app[NAPP];

#endif // WINDOW_H