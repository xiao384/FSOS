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
// 现代 UI 柔和扩展色 (COL_UI_* 160..165, 6-bit DAC 格式, 与 gfx_palette 一致)
#define GFX_COLORS_EXT 24
extern const uint32_t gfx_palette_ext[GFX_COLORS_EXT];

void  gfx_init(void);
// 强制使用 8bpp mode13h 路径 (即便 boot.asm 探测到 VBE 也忽略).
// 用途: WM 等子系统需要"与 vga.c 前端缓冲 (0xA0000) 一致"的兼容模式,
//       以便退出 WM 后 vga 系界面 (菜单/终端) 仍能正常显示.
void  gfx_init_mode13(void);
int   gfx_width(void);
int   gfx_height(void);
int   gfx_is_lfb(void);          // 1 = VBE 线性帧缓冲, 0 = mode13h 8bpp
int   gfx_font_scale(void);      // 字体缩放因子 (LFB=2, mode13h=1)
int   gfx_scale(void);           // 逻辑->原生像素缩放 (GOP 原生>1, 其余=1)

// 真彩接口 (LFB / 桌面)
void gfx_clear_rgb(uint8_t r, uint8_t g, uint8_t b);
void gfx_pixel_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void gfx_fill_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void gfx_rect_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void gfx_text_rgb(int x, int y, const char* s,
                  uint8_t fr, uint8_t fg, uint8_t fb,
                  uint8_t br, uint8_t bg, uint8_t bb);
int  gfx_text_w(const char* s);

// UEFI 原生路径下 vga.c 文本的抗锯齿字形接口
void gfx_draw_char_aa(int x, int y, char ch,
                      uint8_t fr, uint8_t fg, uint8_t fb,
                      uint8_t br, uint8_t bg, uint8_t bb);
void gfx_idx_rgb(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t* b);
void gfx_draw_cjk_aa(int x, int y, const uint16_t* rows,
                     uint8_t fr, uint8_t fg, uint8_t fb,
                     uint8_t br, uint8_t bg, uint8_t bb);
void gfx_draw_cjk24_aa(int x, int y, const uint32_t* rows,
                       uint8_t fr, uint8_t fg, uint8_t fb,
                       uint8_t br, uint8_t bg, uint8_t bb);

// 8bpp 索引接口 (vga.c 兼容, COL_* 索引)
void gfx_clear_idx(uint8_t idx);
void gfx_pixel_idx(int x, int y, uint8_t idx);
void gfx_fill_idx(int x0, int y0, int x1, int y1, uint8_t idx);
void gfx_rect_idx(int x0, int y0, int x1, int y1, uint8_t idx);
void gfx_text_idx(int x, int y, const char* s, uint8_t fg, uint8_t bg);

// 为 mode13h 的附加位图资源设置 DAC 调色板项。LFB 路径无需硬件调色板，调用无副作用。
void gfx_set_palette_rgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

// 圆角矩形原语 (ui_polish): r 内部钳制为 min(r, min(w,h)/4), r=0 退化为直角.
// 逐像素经 gfx_pixel_idx 裁剪越界, 不触发 gfx_flip.
void gfx_fill_round_idx(int x0, int y0, int x1, int y1, int r, uint8_t idx);
void gfx_round_rect_idx(int x0, int y0, int x1, int y1, int r, uint8_t idx);

// 现代 UI 抗锯齿与渐变原语 (modern_ui): AA 锁定边缘 1px, 8bpp 退化为索引原语.
void gfx_line_aa(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void gfx_disc_aa(int cx, int cy, int rr, uint8_t r, uint8_t g, uint8_t b);
void gfx_fill_round_rgb_aa(int x0, int y0, int x1, int y1, int rad, uint8_t r, uint8_t g, uint8_t b);
void gfx_round_rect_rgb_aa(int x0, int y0, int x1, int y1, int rad, uint8_t r, uint8_t g, uint8_t b);
void gfx_gradient_v_rgb(int x0, int y0, int x1, int y1,
                        uint8_t r1, uint8_t g1, uint8_t b1,
                        uint8_t r2, uint8_t g2, uint8_t b2);
void gfx_gradient_v_idx(int x0, int y0, int x1, int y1, uint8_t s, uint8_t e);
uint8_t gfx_trans_idx(uint8_t fg, uint8_t bg);
void gfx_alpha_over_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b, int cov);

// 导出: 透明背景字形 (字形覆盖度直接混合到现有帧, 供玻璃/壁纸表面绘制文字)
void gfx_draw_char_over(int x, int y, char ch, uint8_t fr, uint8_t fg, uint8_t fb);
void gfx_draw_cjk_aa_over(int x, int y, const uint16_t* rows, uint8_t fr, uint8_t fg, uint8_t fb);
void gfx_draw_cjk24_aa_over(int x, int y, const uint32_t* rows, uint8_t fr, uint8_t fg, uint8_t fb);

// 半透明填充 (a=0..256): macOS 玻璃质感顶栏/悬浮面板
void gfx_fill_rgb_alpha(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, int a);
void gfx_fill_round_rgb_alpha(int x0, int y0, int x1, int y1, int rad, uint8_t r, uint8_t g, uint8_t b, int a);

// 毛玻璃模糊 (盒式模糊, LFB 32bpp 路径): 对后台缓冲指定区域做 radius 窗口均值模糊。
// 8bpp 路径下为空实现 (调用方走降级分支)。radius 钳制至 0..8。
void gfx_blur_rgb(int x0, int y0, int x1, int y1, int radius);

// Fast native-GOP wallpaper blit: RGB565 source, cover/crop to destination.
void gfx_blit_rgb565_cover(const uint16_t* src, int sw, int sh, int dw, int dh);

// 桌面壁纸呈现 (LFB 32bpp): 缩放(cover)+上下遮罩烘焙进缓存, 之后每帧仅内存拷贝。
// 相比每帧重跑 30 万像素的 64 位除法缩放, 大幅提升帧率。
// 返回 1 = 已处理; 0 = 非 32bpp LFB 路径 (调用方走 8bpp 程序化回退)。
int  gfx_desktop_wallpaper_present(const uint16_t* src, int sw, int sh,
                                   int top_h, int bot_h, int top_a, int bot_a,
                                   uint8_t mr, uint8_t mg, uint8_t mb);

// 脏区局部重绘: 设置/清除绘制裁剪框 (逻辑坐标, 闭区间; 越界自动钳制到屏幕)。
// wm.c 的合成器在重绘每个脏区域前调用 set_clip, 绘制后 reset_clip。
// 所有像素级原语 (put_px / put_px_real / alpha_blend_native / 矩形填充) 均会遵守,
// 使"仅重绘变化区域"成为可能, 大幅降低整屏合成开销。
void gfx_set_clip(int x0, int y0, int x1, int y1);
void gfx_reset_clip(void);
// 当前是否处于裁剪状态 / 是否与逻辑矩形相交 (供上层跳过无关绘制)
int  gfx_clip_active(void);
int  gfx_clip_intersects(int x0, int y0, int x1, int y1);

void gfx_flip(void);             // 后台缓冲 -> 前台 (翻页)

// UEFI (GOP) 旧兼容呈现入口: 保留用于 BIOS/旧路径诊断.
// 由 PIT 中断周期调用 (内部节流); BIOS 路径无 GOP 信息, 自动失效.
void gfx_gop_present(void);

#endif // GFX_H
