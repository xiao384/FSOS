// gfx.h - 统一图形核心 (8bpp mode13h 或 VBE 32bpp 线性帧缓冲, 双缓冲)
//
// 所有绘制写入后台缓冲, gfx_flip() 整屏翻到前台, 消除闪烁并提速。
// 8bpp 路径用调色板索引 (vga.c 兼容); LFB 路径用 0xRRGGBB 真彩 (C++ 桌面用)。
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

// COL_* 索引对应的真彩色 (0xRRGGBB), 与 mode13h DAC 调色板一致
#define GFX_COLORS 32
extern const uint32_t gfx_palette[GFX_COLORS];

void  gfx_init(void);
// 强制使用 8bpp mode13h 路径 (即便 boot.asm 探测到 VBE 也忽略).
// 用途: WM 等子系统需要"与 vga.c 前端缓冲 (0xA0000) 一致"的兼容模式,
//       以便退出 WM 后 vga 系界面 (菜单/终端) 仍能正常显示.
void  gfx_init_mode13(void);
int   gfx_width(void);
int   gfx_height(void);
int   gfx_is_lfb(void);          // 1 = VBE 线性帧缓冲, 0 = mode13h 8bpp
int   gfx_font_scale(void);      // 字体缩放因子 (LFB=2, mode13h=1)

// 真彩接口 (LFB / 桌面)
void gfx_clear_rgb(uint8_t r, uint8_t g, uint8_t b);
void gfx_pixel_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void gfx_fill_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void gfx_rect_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void gfx_text_rgb(int x, int y, const char* s,
                  uint8_t fr, uint8_t fg, uint8_t fb,
                  uint8_t br, uint8_t bg, uint8_t bb);
int  gfx_text_w(const char* s);

// 8bpp 索引接口 (vga.c 兼容, COL_* 索引)
void gfx_clear_idx(uint8_t idx);
void gfx_pixel_idx(int x, int y, uint8_t idx);
void gfx_fill_idx(int x0, int y0, int x1, int y1, uint8_t idx);
void gfx_rect_idx(int x0, int y0, int x1, int y1, uint8_t idx);
void gfx_text_idx(int x, int y, const char* s, uint8_t fg, uint8_t bg);

void gfx_flip(void);             // 后台缓冲 -> 前台 (翻页)

// UEFI (GOP) 呈现: 把 0xA0000 的 mode13h 前端镜像到 GOP 线性帧缓冲.
// 由 PIT 中断周期调用 (内部节流); BIOS 路径无 GOP 信息, 自动失效.
void gfx_gop_present(void);

#endif // GFX_H
