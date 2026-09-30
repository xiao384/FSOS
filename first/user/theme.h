// theme.h - FSOS 桌面主题集中声明 (modern_ui v1.4)
//
// 全部视觉参数集中于此: 圆角半径、可读对比组合、字体规格、间距网格、
// 阴影规格、图标模板、明度层级。
// 禁止在 wm.gui/app/taskmgr 中散落硬编码视觉参数; 新增视觉参数先在此登记。
#ifndef THEME_H
#define THEME_H

#include "vga.h"   // COL_* 与 VGA_W/SCREEN_H
#include "icon.h"  // ICON_SIZE (图标模板网格基准)
#include "theme_api.h"  // 运行时 Theme API (gui_framework Phase 2)

// ---- 分辨率缩放因子 (hires) ----
// THEME_SCALE = gfx_font_scale(): 1=320x200, 2=640x480, 3=1280x720, 4=1920x1080
// THEME_SF = 逻辑 UI 缩放。GFX 原生路径负责把整个逻辑桌面放大到 GOP，
// 因此这里不能再次按屏幕分辨率乘倍，否则会产生过大的控件/文字。
// 保留宏名称以兼容现有应用。
#define THEME_SCALE (gfx_font_scale())
#define THEME_SF    (theme_get_scale_factor())

// ---- 圆角半径：窗口/面板采用现代 8px 级层次，控件更紧凑，最大化=0 ----
// gui_framework Phase 2: 委托至运行时 Theme API, 支持主题切换与分辨率自适应。
// 兼容性: theme_init() 调用后返回值与原编译期常量等价。
#define RADIUS_WINDOW (theme_get_radius(RADIUS_WINDOW_T))
#define RADIUS_PANEL  (theme_get_radius(RADIUS_PANEL_T))
#define RADIUS_CTRL   (theme_get_radius(RADIUS_CTRL_T))

// ---- 可读对比组合 (fg, bg 两个调色板索引, 均来自 0..31 已登记) ----
// 面板浅蓝灰底 -> 白字; 强调蓝底 -> 白字; 任务栏白底 -> 黑字
#define TXT_ON_PANEL    COL_WHITE, COL_PANEL
#define TXT_ON_ACCENT   COL_WHITE, COL_ACCENT
#define TXT_ON_TASKBAR  COL_BLACK, COL_TASKBAR
#define TXT_ON_WALL     COL_WHITE, COL_WALL_D
#define TXT_ON_WHITE    COL_BLACK, COL_TASKBAR
#define TXT_ON_TITLE    COL_WHITE, COL_TITLEBG
#define TXT_ON_FIELD    COL_LGRAY, COL_FIELD
#define TXT_ON_HI       COL_WHITE, COL_PANEL_HI

// ---- 字体规格：UI 中文保持 16x16 完整字形，ASCII 8x8 垂直居中 ----
#define FONT_CJK_W   (theme_get_font_w(FONT_TOKEN_CJK))
#define FONT_CJK_H   (theme_get_font_h(FONT_TOKEN_CJK))
#define FONT_ASCII_W (theme_get_font_w(FONT_TOKEN_ASCII))
#define FONT_ASCII_H (theme_get_font_h(FONT_TOKEN_ASCII))
#define LINE_H       (theme_get_font_h(FONT_TOKEN_BODY))

// ==================== modern_ui v1.4 新增 ====================

// ---- 间距网格 (spec 6.3.2 / 方向五: 2px 基数, 间距为网格整数倍) ----
// gui_framework Phase 2: 委托至运行时 Theme API。
#define GRID_UNIT  (theme_get_padding(PADDING_UNIT))   // 基础单位
#define GRID_SM    (theme_get_padding(PADDING_SM))     // 小档 (控件内边距等)
#define GRID_MD    (theme_get_padding(PADDING_MD))     // 中档 (控件间距)
#define GRID_LG    (theme_get_padding(PADDING_LG))     // 大档 (分组间距/区块)
#define PAD_CTRL   GRID_SM   // 控件最小内边距
#define PAD_PANEL  GRID_MD   // 面板内边距
#define GAP_CTRL   GRID_MD   // 控件间间距
#define GAP_SECTION GRID_LG  // 分组间距

// ---- 阴影规格 (spec 6.3.3 / 方向五: 窗口 > 面板 > 控件递减) ----
// gui_framework Phase 2: 委托至运行时 Theme API。
#define SHADOW_OFF   (theme_get_shadow_off())    // 统一偏移 (px)
#define SHADOW_COL   (theme_get_shadow_col())    // 投影/描边色 (调色板索引)
#define SHADOW_FOCUS 1         // 聚焦档: 2px 强阴影 (偏移 SHADOW_OFF)
#define SHADOW_NORM  0         // 非聚焦档: 1px 弱阴影
// 取档规则: 窗口按聚焦取档; 对话框默认 SHADOW_NORM; 按钮不投影。

// ---- 图标模板 (spec 5.1 / 方向一: 统一 32x32 模板) ----
#define ICN_GRID        ICON_SIZE        // 图标网格 32
#define ICN_RADIUS      RADIUS_PANEL     // 外框圆角 4px
#define ICN_CONTENT_PCT_MIN 50           // 内容区占比下限 (%) 
#define ICN_CONTENT_PCT_MAX 70           // 内容区占比上限 (%)

// ---- 明度层级 (spec 5.5.1.4 / 方向五: 壁纸<面板<标题栏/聚焦 单调) ----
#define LUM_BG    0   // 壁纸 (最暗)
#define LUM_PANEL 1   // 面板
#define LUM_TITLE 2   // 标题栏
#define LUM_FOCUS 3   // 聚焦/按下 (最亮)

#endif // THEME_H
