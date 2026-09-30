// cjk.c - 中英混排文本绘制 (8bpp, 后台缓冲)
// ASCII 字符使用 0xB0000 的 8x8 字体 (BIOS 加载器 / UEFI 加载器均装入);
// 汉字使用内嵌 16x16 常规字库与 24x24 Shell/UI 字库 (见 cjk_font.h)。
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
    int aw = (gfx_scale() > 1) ? 8 : 8 * gfx_font_scale();
    while (*s) {
        uint32_t cp = utf8_next(&s);
        w += cjk_is_cjk(cp) ? CJK_CHAR_W : aw;
    }
    return w;
}

static void draw_ascii(int x, int y, unsigned char ch, uint8_t fg, uint8_t bg) {
    if (ch < 0x20) {           // 控制字符: 画空白格子 (保底间距)
        gfx_fill_idx(x, y, x + 7, y + 7, bg);
        return;
    }
    if (ch >= 128) ch = '?';   // 内置字体只覆盖可打印 ASCII (0x20..0x7F)
    // 原生高分路径: 走 gfx.c 的抗锯齿字形 (8x8 位图双线性覆盖混合)
    if (gfx_scale() > 1) {
        uint8_t fr, fgg, fb, br, bgg, bb;
        gfx_idx_rgb(fg, &fr, &fgg, &fb);
        gfx_idx_rgb(bg, &br, &bgg, &bb);
        gfx_draw_char_aa(x, y, (char)ch, fr, fgg, fb, br, bgg, bb);
        return;
    }
    const uint8_t* gl = (const uint8_t*)g_font8x8 + (uint32_t)(ch - 0x20) * 8;
    // 与 vga_draw_char 完全一致：GOP 原生路径直接把 8x8 字形放大到 font_scale。
    int sc = gfx_font_scale();
    if (sc < 1) sc = 1;
    for (int col = 0; col < 8; col++) {
        uint8_t bits = gl[col];
        for (int row = 0; row < 8; row++) {
            uint8_t c = ((bits >> row) & 1) ? fg : bg;
            int px = x + col * sc, py = y + row * sc;
            for (int dy = 0; dy < sc; dy++)
                for (int dx = 0; dx < sc; dx++)
                    gfx_pixel_idx(px + dx, py + dy, c);
        }
    }
}

static void draw_cjk(int x, int y, uint32_t cp, uint8_t fg, uint8_t bg) {
    const uint16_t* gl = cjk_lookup(cp);
    // 原生高分路径: 走 gfx.c 的抗锯齿 CJK 字形 (16x16 位图双线性覆盖混合)
    if (gfx_scale() > 1) {
        uint8_t fr, fgg, fb, br, bgg, bb;
        gfx_idx_rgb(fg, &fr, &fgg, &fb);
        gfx_idx_rgb(bg, &br, &bgg, &bb);
        if (!gl) {
            // 缺字占位符: 16×16 方框 (fg 描边 + bg 填充)
            gfx_fill_idx(x, y, x + CJK_CHAR_W - 1, y + CJK_CHAR_H - 1, bg);
            gfx_rect_idx(x, y, x + CJK_CHAR_W - 1, y + CJK_CHAR_H - 1, fg);
            return;
        }
        gfx_draw_cjk_aa(x, y, gl, fr, fgg, fb, br, bgg, bb);
        return;
    }
    // 背景格先铺满 (中文高 16, 可能超出所在行)
    gfx_fill_idx(x, y, x + CJK_CHAR_W - 1, y + CJK_CHAR_H - 1, bg);
    if (!gl) {
        // 缺字占位符: 16×16 方框 (fg 描边 + bg 填充)
        gfx_rect_idx(x, y, x + CJK_CHAR_W - 1, y + CJK_CHAR_H - 1, fg);
        return;
    }
    for (int row = 0; row < CJK_CHAR_H; row++) {
        uint16_t bits = gl[row];
        for (int col = 0; col < CJK_CHAR_W; col++) {
            if ((bits >> (15 - col)) & 1) gfx_pixel_idx(x + col, y + row, fg);
        }
    }
}

static void draw_cjk_ui(int x, int y, uint32_t cp, uint8_t fg, uint8_t bg) {
    const uint32_t* gl24 = cjk_lookup24(cp);
    const uint16_t* gl16 = cjk_lookup(cp);
    int use24 = (gfx_font_scale() >= 2 && gl24);
    if (use24) {
        uint8_t fr,fgg,fb,br,bgg,bb;
        gfx_idx_rgb(fg,&fr,&fgg,&fb); gfx_idx_rgb(bg,&br,&bgg,&bb);
        gfx_draw_cjk24_aa(x,y,gl24,fr,fgg,fb,br,bgg,bb);
        return;
    }
    gfx_fill_idx(x,y,x+CJK_UI_CHAR_W-1,y+CJK_UI_CHAR_H-1,bg);
    if (!gl16) {
        gfx_rect_idx(x,y,x+CJK_UI_CHAR_W-1,y+CJK_UI_CHAR_H-1,fg);
        return;
    }
    for (int row=0; row<16; row++) {
        uint16_t bits=gl16[row];
        for (int col=0; col<16; col++) if ((bits>>(15-col))&1) gfx_pixel_idx(x+col,y+row,fg);
    }
}

int cjk_ui_text_w(const char* s) {
    int w=0;
    int cw = (gfx_font_scale() >= 2) ? 24 : 16;
    int aw = (gfx_font_scale() >= 2) ? 16 : 8;
    while (*s) {
        uint32_t cp=utf8_next(&s);
        w += cjk_is_cjk(cp) ? cw : aw;
    }
    return w;
}

void cjk_ui_text(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    int cw = (gfx_font_scale() >= 2) ? 24 : 16;
    int aw = (gfx_font_scale() >= 2) ? 16 : 8;
    while (*s) {
        uint32_t cp=utf8_next(&s);
        if (!cjk_is_cjk(cp)) {
            unsigned char ch=(unsigned char)cp;
            if (cp==0x2022 || cp==0x00B7) ch='-';
            else if (cp==0x2190 || cp==0x2192 || cp==0x203A) ch='>';
            else if (cp==0x2191) ch='^';
            else if (cp==0x2193) ch='v';
            else if (cp>0x7F) ch='?';
            gfx_fill_idx(x,y,x+aw-1,y+cw-1,bg);
            draw_ascii(x, y + (cw-8)/2, ch, fg, bg);
            x += aw;
        } else {
            draw_cjk_ui(x,y,cp,fg,bg);
            x += cw;
        }
    }
}

// 透明背景 UI 文本: 字形覆盖度直接混合到现有帧, 不铺背景格。
// 供 macOS 玻璃顶栏 / 壁纸桌面图标标签等半透明表面使用。
void cjk_ui_text_over(int x, int y, const char* s, uint8_t fg) {
    uint8_t fr,fgg,fb; gfx_idx_rgb(fg,&fr,&fgg,&fb);
    int cw = (gfx_font_scale() >= 2) ? 24 : 16;
    int aw = (gfx_font_scale() >= 2) ? 16 : 8;
    while (*s) {
        uint32_t cp=utf8_next(&s);
        if (!cjk_is_cjk(cp)) {
            unsigned char ch=(unsigned char)cp;
            if (cp==0x2022 || cp==0x00B7) ch='-';
            else if (cp==0x2190 || cp==0x2192 || cp==0x203A) ch='>';
            else if (cp==0x2191) ch='^';
            else if (cp==0x2193) ch='v';
            else if (cp>0x7F) ch='?';
            if (ch >= 0x20) {
                if (gfx_scale() > 1)
                    gfx_draw_char_over(x, y + (cw-8)/2, (char)ch, fr, fgg, fb);
                else {
                    // scale=1 (mode13h): 退化为不透明格子
                    gfx_fill_idx(x,y,x+aw-1,y+cw-1,0);
                    draw_ascii(x, y + (cw-8)/2, ch, fg, 0);
                }
            }
            x += aw;
        } else {
            const uint32_t* gl24 = cjk_lookup24(cp);
            const uint16_t* gl16 = cjk_lookup(cp);
            int use24 = (gfx_font_scale() >= 2 && gl24);
            if (gfx_scale() > 1) {
                if (use24) gfx_draw_cjk24_aa_over(x, y, gl24, fr, fgg, fb);
                else if (gl16) gfx_draw_cjk_aa_over(x, y, gl16, fr, fgg, fb);
                else { /* 缺字: 空心方框, 仅描边 */
                    uint8_t br,bgc,bb; gfx_idx_rgb(0,&br,&bgc,&bb);
                    (void)br;(void)bgc;(void)bb;
                    for (int i=0;i<cw;i++){ gfx_alpha_over_rgb(x+i,y,fr,fgg,fb,256); gfx_alpha_over_rgb(x+i,y+cw-1,fr,fgg,fb,256); }
                    for (int j=0;j<cw;j++){ gfx_alpha_over_rgb(x,y+j,fr,fgg,fb,256); gfx_alpha_over_rgb(x+cw-1,y+j,fr,fgg,fb,256); }
                }
            } else {
                draw_cjk_ui(x,y,cp,fg,0);
            }
            x += cw;
        }
    }
}

int cjk_ui_text_ellipsis(int x, int y, const char* s, int max_w,
                         uint8_t fg, uint8_t bg) {
    if (max_w<=0) return 0;
    int total=cjk_ui_text_w(s);
    if (total<=max_w) { cjk_ui_text(x,y,s,fg,bg); return total; }
    const char* p=s; int w=0; int cw=(gfx_font_scale()>=2)?24:16; int aw=(gfx_font_scale()>=2)?16:8;
    while (*p) {
        const char* q=p; uint32_t cp=utf8_next(&q); int ch_w=cjk_is_cjk(cp)?cw:aw;
        if (w+ch_w+aw>max_w) break;
        w+=ch_w; p=q;
    }
    char part[128]; int n=(int)(p-s); if(n>126)n=126; for(int i=0;i<n;i++)part[i]=s[i]; part[n]=0;
    cjk_ui_text(x,y,part,fg,bg);
    if (w+aw<=max_w) { cjk_ui_text(x+w,y,"..",fg,bg); return w+aw; }
    return w;
}

// 透明背景截断版: 超宽时截断并追加 ".."
int cjk_ui_text_ellipsis_over(int x, int y, const char* s, int max_w, uint8_t fg) {
    if (max_w<=0) return 0;
    int total=cjk_ui_text_w(s);
    if (total<=max_w) { cjk_ui_text_over(x,y,s,fg); return total; }
    const char* p=s; int w=0; int cw=(gfx_font_scale()>=2)?24:16; int aw=(gfx_font_scale()>=2)?16:8;
    while (*p) {
        const char* q=p; uint32_t cp=utf8_next(&q); int ch_w=cjk_is_cjk(cp)?cw:aw;
        if (w+ch_w+aw>max_w) break;
        w+=ch_w; p=q;
    }
    char part[128]; int n=(int)(p-s); if(n>126)n=126; for(int i=0;i<n;i++)part[i]=s[i]; part[n]=0;
    cjk_ui_text_over(x,y,part,fg);
    if (w+aw<=max_w) { cjk_ui_text_over(x+w,y,"..",fg); return w+aw; }
    return w;
}

void cjk_text(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    int aw = (gfx_scale() > 1) ? 8 : 8 * gfx_font_scale();
    int ah = (gfx_scale() > 1) ? 8 : 8 * gfx_font_scale();
    int off = (CJK_CHAR_H - ah) / 2;
    if (off < 0) off = 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        if (!cjk_is_cjk(cp)) {
            draw_ascii(x, y + off, (unsigned char)cp, fg, bg);
            x += aw;
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
        if (w + cw + 16 > max_w) break;   // 预留 ".." 空间 (2 个 ASCII = 16px)
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
    return w + 16;
}
