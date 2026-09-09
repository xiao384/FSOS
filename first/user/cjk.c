// cjk.c - 中英混排文本绘制 (8bpp, 后台缓冲)
// ASCII 字符使用 0xB0000 的 8x8 字体 (BIOS 加载器 / UEFI 加载器均装入);
// 汉字使用内嵌 12x12 点阵字库 (见 cjk_font.h)。
#include "cjk.h"
#include "gfx.h"
#include "vga.h"
#include "cjk_font.h"
// UEFI 无 int10h 且 0xB0000 (VGA/MMIO 窗口) 读回的字形不可靠, 与 vga.c/gfx.c 保持一致,
// ASCII 一律从内核内置字体 boot/uefi/font8x8.h 的 g_font8x8[96][8] 取 (按 ch-0x20 索引)。
#include "boot/uefi/font8x8.h"

#define FONT8_ADDR 0xB0000UL      // 8x8 ASCII 字体基址 (gfx/vga 共用约定; 仅 BIOS 路径有用)

// UTF-8 解码: 返回码点并推进指针; *p 到字符串尾返回 0
static uint32_t utf8_next(const char** p) {
    const uint8_t* s = (const uint8_t*)*p;
    uint8_t c = *s;
    if (c == 0) return 0;
    if (c < 0x80) { (*p)++; return c; }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *p += 2;
        return ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *p += 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) |
               (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *p += 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
               ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    (*p)++;                    // 非法字节: 跳过, 视为 ASCII '?'
    return '?';
}

int cjk_is_cjk(uint32_t cp) {
    // CJK 统一表意文字 + 扩展常用区 + 全角符号统一视为宽字符
    if (cp >= 0x2E80 && cp <= 0x9FFF) return 1;
    if (cp >= 0x3000 && cp <= 0x30FF) return 1;    // CJK 标点/片假名
    if (cp >= 0xFF00 && cp <= 0xFFEF) return 1;    // 全角 ASCII/符号
    return 0;
}

int cjk_text_w(const char* s) {
    int w = 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        w += cjk_is_cjk(cp) ? CJK_CHAR_W : 8;
    }
    return w;
}

static void draw_ascii(int x, int y, unsigned char ch, uint8_t fg, uint8_t bg) {
    if (ch < 0x20) {           // 控制字符: 画空白格子 (保底间距)
        gfx_fill_idx(x, y, x + 7, y + 7, bg);
        return;
    }
    if (ch >= 128) ch = '?';   // 内置字体只覆盖可打印 ASCII (0x20..0x7F)
    const uint8_t* gl = (const uint8_t*)g_font8x8 + (uint32_t)(ch - 0x20) * 8;
    // boot/uefi/font8x8.h 数据来自 MicroPython font_petme128_8x8:
    // 每个字 8 字节, 每字节是一列, bit0 = 最上像素
    for (int col = 0; col < 8; col++) {
        uint8_t bits = gl[col];
        for (int row = 0; row < 8; row++) {
            int on = (bits >> row) & 1;
            if (on) gfx_pixel_idx(x + col, y + row, fg);
            else    gfx_pixel_idx(x + col, y + row, bg);
        }
    }
}

static void draw_cjk(int x, int y, uint32_t cp, uint8_t fg, uint8_t bg) {
    const uint16_t* gl = cjk_lookup(cp);
    // 背景格先铺满 (中文高 12, 可能超出所在行)
    gfx_fill_idx(x, y, x + CJK_CHAR_W - 1, y + CJK_CHAR_H - 1, bg);
    if (!gl) return;           // 缺字: 只画空位
    for (int row = 0; row < CJK_CHAR_H; row++) {
        uint16_t bits = gl[row];
        for (int col = 0; col < CJK_CHAR_W; col++) {
            if ((bits >> (11 - col)) & 1) gfx_pixel_idx(x + col, y + row, fg);
        }
    }
}

void cjk_text(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    while (*s) {
        uint32_t cp = utf8_next(&s);
        if (!cjk_is_cjk(cp)) {
            // ASCII 8px 高, 相对汉字垂直居中偏移 2px
            draw_ascii(x, y + 2, (unsigned char)cp, fg, bg);
            x += 8;
        } else {
            draw_cjk(x, y, cp, fg, bg);
            x += CJK_CHAR_W;
        }
    }
}

int cjk_text_ellipsis(int x, int y, const char* s, int max_w,
                      uint8_t fg, uint8_t bg) {
    // 先量, 若超宽则逐字符追加到容量, 最后画 ".."
    int total = cjk_text_w(s);
    if (total <= max_w) {
        cjk_text(x, y, s, fg, bg);
        return total;
    }
    const char* p = s;
    int w = 0;
    while (*p) {
        const char* q = p;
        uint32_t cp = utf8_next(&q);
        int cw = cjk_is_cjk(cp) ? CJK_CHAR_W : 8;
        if (w + cw + 12 > max_w) break;   // 预留 ".." 空间
        w += cw;
        p = q;
    }
    char part[64];
    int n = (int)(p - s);
    if (n > 62) n = 62;
    for (int i = 0; i < n; i++) part[i] = s[i];
    part[n] = 0;
    cjk_text(x, y, part, fg, bg);
    cjk_text(x + w, y, "..", fg, bg);
    return w + 12;
}
