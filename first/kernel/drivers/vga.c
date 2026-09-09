// vga.c - VGA 图形绘制原语 (经 gfx 核心适配 mode13h 8bpp 或 VBE LFB 32bpp)
// vga_* 接口接受 COL_* 调色板索引, 内部委托 gfx_*_idx 写入 gfx 后台缓冲,
// 由 UI 循环的 gfx_flip() 翻到前台。mode13h/LFB 路径对调用者透明。
#include "vga.h"
#include "io.h"
#include "gfx.h"       // gfx_font_scale() 字体缩放
#include <stddef.h>
// UEFI 无 int10h 时 0xB0000 (VGA/MMIO 窗口) 读回的字形不可靠, 改用内核内置字体
// (boot/uefi/font8x8.h 的 g_font8x8[96][8], 按 ch-0x20 索引), BIOS/UEFI 路径一致。
#include "boot/uefi/font8x8.h"

static int font_ok = 0;

// 检查字体区有效性 (第 0x41 字符 'A' 应有明显的位图)
static void check_font(void) {
    const uint8_t* font = (const uint8_t*)g_font8x8;   // 内置字体, 按 (ch-0x20)*8 索引
    uint8_t sum = 0;
    for (int i = 0; i < 8; i++) sum |= font[(0x41 - 0x20) * 8 + i];
    font_ok = (sum != 0);
}

void vga_init(void) {
    gfx_init();            // 统一显示初始化: VBE LFB (640x480x32) 或回退 mode13h
    check_font();
    vga_clear(COL_BLUE);

}

void vga_clear(uint8_t color) {
    gfx_clear_idx(color);
}

void vga_pixel(int x, int y, uint8_t c) {
    gfx_pixel_idx(x, y, c);
}

void vga_fill_rect(int x0, int y0, int x1, int y1, uint8_t c) {
    gfx_fill_idx(x0, y0, x1, y1, c);
}


void vga_draw_rect(int x0, int y0, int x1, int y1, uint8_t c) {
    vga_fill_rect(x0, y0, x1, y0, c);
    vga_fill_rect(x0, y1, x1, y1, c);
    vga_fill_rect(x0, y0, x0, y1, c);
    vga_fill_rect(x1, y0, x1, y1, c);
}

void vga_draw_char(int x, int y, char ch, uint8_t fg, uint8_t bg) {
    const uint8_t* font = (const uint8_t*)g_font8x8;   // 内置字体, 按 (ch-0x20)*8 索引
    if ((uint8_t)ch >= 128) ch = '?';
    if ((uint8_t)ch < 0x20) ch = 0x20;                 // 控制字符 -> 空白 (原 0xB0000 空区)
    int sc = gfx_font_scale();
    // font8x8.h 数据为 MicroPython petme128: 每字节一列, bit0=最上; sc x sc 块渲染
    for (int col = 0; col < 8; col++) {
        uint8_t bits = font[((uint8_t)ch - 0x20) * 8 + col];
        for (int row = 0; row < 8; row++) {
            uint8_t c = (bits >> row) & 1 ? fg : bg;
            int px = x + col * sc, py = y + row * sc;
            for (int dy = 0; dy < sc; dy++)
                for (int dx = 0; dx < sc; dx++)
                    vga_pixel(px + dx, py + dy, c);
        }
    }
}

void vga_draw_text(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    int sc = gfx_font_scale();
    while (*s) {
        vga_draw_char(x, y, *s, fg, bg);
        x += FONT_W * sc;
        s++;
    }
}

void vga_draw_text_center(int y, const char* s, uint8_t fg, uint8_t bg) {
    int w = vga_text_w(s);
    vga_draw_text((VGA_W - w) / 2, y, s, fg, bg);
}

int vga_text_w(const char* s) {
    int n = 0;
    while (*s) { n++; s++; }
    return n * FONT_W * gfx_font_scale();
}

int vga_font_valid(void) { return font_ok; }
