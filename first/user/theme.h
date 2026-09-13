// theme.h - FSOS 桌面主题集中声明 (modern_ui v1.4)
//
// 全部视觉参数集中于此: 圆角半径、可读对比组合、字体规格、间距网格、
// 阴影规格、图标模板、明度层级。
// 禁止在 wm.gui/app/taskmgr 中散落硬编码视觉参数; 新增视觉参数先在此登记。
#ifndef THEME_H
#define THEME_H

#include "vga.h"   // COL_* 与 VGA_W/SCREEN_H
#include "icon.h"  // ICON_SIZE (图标模板网格基准)

// ---- 分辨率缩放因子 (hires) ----
// THEME_SCALE = gfx_font_scale(): 1=320x200, 2=640x480, 3=1280x720, 4=1920x1080
// THEME_SF = 视觉缩放: 1 at 320x200/640x480, 2 at 1280x720+, 保持低分辨率外观不变
#define THEME_SCALE (gfx_font_scale())
#define THEME_SF    (((THEME_SCALE) + 1) / 2)

// ---- 圆角半径 (spec 6.1: 窗口/面板 2~4px, 控件 <= 短边/4, 最大化=0) ----
// 在 320x200 的低分辨率下保持克制的圆角；圆角过大反而会吃掉控件可点区域。
// hires: 高分辨率下按 THEME_SF 放大圆角, 保持视觉占比合理。
#define RADIUS_WINDOW (4 * THEME_SF)
#define RADIUS_PANEL  (4 * THEME_SF)
#define RADIUS_CTRL   (3 * THEME_SF)

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

// ---- 字体规格 (spec 6.2: 汉字 16x16, ASCII 8x8, 行距 >= 16px) ----
#define FONT_CJK_W   16
#define FONT_CJK_H   16
#define FONT_ASCII_W 8
#define FONT_ASCII_H 8
#define LINE_H       18

// ==================== modern_ui v1.4 新增 ====================

// ---- 间距网格 (spec 6.3.2 / 方向五: 2px 基数, 间距为网格整数倍) ----
// hires: 按 THEME_SF 放大间距, 高分辨率下视觉占比合理
#define GRID_UNIT  (2 * THEME_SF)   // 基础单位
#define GRID_SM    (4 * THEME_SF)   // 小档 (控件内边距等)
#define GRID_MD    (8 * THEME_SF)   // 中档 (控件间距)
#define GRID_LG    (16 * THEME_SF)  // 大档 (分组间距/区块)
#define PAD_CTRL   GRID_SM   // 控件最小内边距
#define PAD_PANEL  GRID_MD   // 面板内边距
#define GAP_CTRL   GRID_MD   // 控件间间距
#define GAP_SECTION GRID_LG  // 分组间距

// ---- 阴影规格 (spec 6.3.3 / 方向五: 窗口 > 面板 > 控件递减) ----
#define SHADOW_OFF   (2 * THEME_SF)    // 统一偏移 (px)
#define SHADOW_COL   COL_SHADOW // 投影/描边色 (索引 31)
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
