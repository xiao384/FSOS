// vga.h - VGA 图形绘制原语 (mode13h 8bpp 或 VBE LFB 32bpp, 经 gfx 核心适配)
#ifndef VGA_GFX_H
#define VGA_GFX_H

#include <stdint.h>
#include "gfx.h"          // gfx_width()/gfx_height()/gfx_*_idx() 适配 LFB

// ---- 自定义调色板颜色常量 (mode 13h 索引) ----
#define COL_BLACK    0
#define COL_BLUE     1
#define COL_GREEN    2
#define COL_CYAN     3
#define COL_RED      4
#define COL_MAGENTA  5
#define COL_BROWN    6
#define COL_LGRAY    7
#define COL_DGRAY    8
#define COL_LBLUE    9
#define COL_LGREEN   10
#define COL_LCYAN    11
#define COL_LRED     12
#define COL_LMAG     13
#define COL_YELLOW   14
#define COL_WHITE    15
#define COL_PANEL    16   // 面板浅蓝灰
#define COL_PANEL_HI 17   // 选中/高亮蓝
#define COL_TITLEBG  18   // 标题栏深蓝
#define COL_ORANGE   19
#define COL_FIELD    20   // 输入框底色

// ---- 桌面 UI 主题扩展色 (索引 21..31, 与 gfx.c gfx_palette / vga.c 同步) ----
// "Aurora" 桌面主题：macOS 的夜色玻璃感为主，保留 Windows 11 的清晰强调色。
#define COL_WALL_A   21   // 壁纸渐变色 A (最亮天蓝)
#define COL_WALL_B   22
#define COL_WALL_C   23
#define COL_WALL_D   24
#define COL_WALL_E   25
#define COL_WALL_F   26   // 壁纸渐变色 F (最深)
#define COL_ACCENT   27   // 强调蓝 (标题栏/选中/按钮, 类 Windows 蓝)
#define COL_ACCENT_SOFT 28 // 柔和蓝 (悬停/浅蓝底)
#define COL_TASKBAR  29   // Dock / 菜单的浅色玻璃替代色
#define COL_TASK_HI  30   // Dock / 菜单悬停色
#define COL_SHADOW   31   // 阴影/描边/深分隔

// ---- 现代 UI 柔和扩展色索引 (160..165, modern_ui; 与 gfx.c gfx_palette_ext / DAC 注入同步) ----
// 0..31 旧语义与壁纸扩展 32..159 占用保持不动; COL_UI_* 供界面层统一引用柔和语义色。
#define COL_UI_BASE       160
#define COL_UI_SOFT_OK    160   // 柔和绿 (成功)
#define COL_UI_SOFT_ERR   161   // 柔和红 (错误)
#define COL_UI_SOFT_WARN  162   // 柔和琥珀 (警告)
#define COL_UI_BG_SOFT    163   // 夜色蓝灰 (大面积背景)
#define COL_UI_TITLE_SOFT 164   // 标题栏深蓝 (聚焦)
#define COL_UI_DOCK_HI_SOFT 165 // Dock 悬停浅蓝

#define VGA_W (gfx_width())
#define SCREEN_H (gfx_height())
#define FONT_W 8
#define FONT_H 8

// 8x8 字体表首地址 (boot.asm 从 VGA ROM 拷到 0xB0000;
// 0xB0000 是 VGA 显存高 64KB, mode13h (0xA0000-0xAFFFF) 不使用;
// 不能放 0x80000/0xC000 等低端地址, 会被内核缓冲或第二段装载区占用)
#define FONT_ADDR 0xB0000

void vga_init(void);
void vga_clear(uint8_t color);
void vga_pixel(int x, int y, uint8_t c);
void vga_fill_rect(int x0, int y0, int x1, int y1, uint8_t c);
void vga_draw_rect(int x0, int y0, int x1, int y1, uint8_t c);
void vga_fill_round_rect(int x0, int y0, int x1, int y1, int r, uint8_t c);
void vga_draw_round_rect(int x0, int y0, int x1, int y1, int r, uint8_t c);
void vga_draw_char(int x, int y, char ch, uint8_t fg, uint8_t bg);
void vga_draw_text(int x, int y, const char* s, uint8_t fg, uint8_t bg);
void vga_draw_text_center(int y, const char* s, uint8_t fg, uint8_t bg);
int  vga_text_w(const char* s);

// 现代 UI 渐变与混色委托层 (modern_ui): 按显示模式委托 gfx 原语
void vga_gradient_v(int x0, int y0, int x1, int y1, uint8_t s, uint8_t e);
void vga_alpha_over(int x, int y, uint8_t idx, int alpha);

#endif // VGA_GFX_H
