// gfx.c - 统一图形核心
//
// 显示模式:
//   - 优先 VBE 线性帧缓冲 (boot.asm 探测, 信息存 0x6400). 32/16bpp 真彩.
//   - 回退 mode13h 320x200x256 (8bpp, 调色板索引).
// 双缓冲: 所有绘制写入 g_back, gfx_flip() 整屏拷贝到前台帧缓冲.
#include "gfx.h"
#include <stdint.h>
#include "kheap.h"
#include "io.h"          // outb/inb (串口调试)
// UEFI 无 int10h 时 0xB0000 (VGA/MMIO 窗口) 读回的字形不可靠, 改用内核内置字体
// (boot/uefi/font8x8.h 的 g_font8x8[96][8], 按 ch-0x20 索引), BIOS/UEFI 路径一致。
#include "boot/uefi/font8x8.h"

// VBE 信息结构 (boot.asm 写入物理 0x6400)
#define VBE_INFO 0x6400UL
#define FONT_ADDR 0xB0000UL

// COL_* 真彩色 (0xRRGGBB), 与 mode13h DAC 一致
const uint32_t gfx_palette[GFX_COLORS] = {
    0x000000, // 0 black
    0x000028, // 1 blue (0,0,40)
    0x002800, // 2
    0x002828, // 3
    0x280000, // 4
    0x280028, // 5
    0x282800, // 6
    0x242424, // 7 lgray (36,36,36)
    0x101010, // 8 dgray (16,16,16)
    0x00003F, // 9 bright blue (0,0,63)
    0x003F00, // 10
    0x003F3F, // 11
    0x3F0000, // 12
    0x3F003F, // 13
    0x3F3F00, // 14
    0x3F3F3F, // 15 white
    0x181C28, // 16 panel (24,28,40)
    0x24486E, // 17 panel hi (36,72,110)
    0x101424, // 18 titlebg (16,20,36)
    0x3F2800, // 19 orange (63,40,0)
    0x00241C, // 20 field (0,36,28)
    // ---- UI 主题扩展色 21..31 (与 vga.h COL_* 同步, 勿乱改旧 0..20) ----
    0x122236, // 21 WALL_A  (72,138,219) 壁纸渐变最亮天蓝
    0x0F1E33, // 22 WALL_B  (62,122,205)
    0x0D1B2F, // 23 WALL_C  (52,108,190)
    0x0A172B, // 24 WALL_D  (42,94,175)
    0x081428, // 25 WALL_E  (34,80,160)
    0x061024, // 26 WALL_F  (26,66,146) 壁纸渐变最深
    0x001D35, // 27 ACCENT   (0,118,213) Windows 蓝 (标题栏/按钮)
    0x0A1934, // 28 ACCENT_SOFT 柔和蓝 (选中底色/悬停)
    0x3C3C3D, // 29 TASKBAR (240,242,246) 任务栏/菜单白底
    0x34383D, // 30 TASK_HI (208,224,244) 任务栏选中浅蓝灰
    0x0A0B0F, // 31 SHADOW  (40,46,60) 深描边/分隔
};

static int      g_lfb = 0;
static int      g_w = 0, g_h = 0, g_pitch = 0, g_bpp = 0, g_bpp_bytes = 0;
static uint8_t  g_rshift = 16, g_gshift = 8, g_bshift = 0, g_rsize = 8;
static uint8_t* g_back = 0;        // 后台缓冲 (内核堆)
static void*    g_front = 0;       // 前台物理帧缓冲
static int      g_font_scale = 1;  // 字体缩放 (LFB=2 提升清晰度, mode13h=1)

int gfx_font_scale(void) { return g_font_scale; }

// ---- 像素格式打包 (0xRRGGBB -> 当前帧缓冲像素值) ----
static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (g_rsize >= 8) {
        return ((uint32_t)r << g_rshift) | ((uint32_t)g << g_gshift) | ((uint32_t)b << g_bshift);
    } else {
        uint32_t rv = (r >> (8 - g_rsize)) & ((1u << g_rsize) - 1);
        uint32_t gv = (g >> (8 - 6)) & 0x3F;   // 简化: 绿通常 6 位
        uint32_t bv = (b >> (8 - g_rsize)) & ((1u << g_rsize) - 1);
        return (rv << g_rshift) | (gv << g_gshift) | (bv << g_bshift);
    }
}

static inline void put_px(int x, int y, uint32_t px) {
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    uint8_t* p = g_back + (size_t)y * g_pitch + (size_t)x * g_bpp_bytes;
    if (g_bpp_bytes == 4)      *(uint32_t*)p = px;
    else if (g_bpp_bytes == 2) *(uint16_t*)p = (uint16_t)px;
    else                       *p = (uint8_t)px;
}

// ---- mode13h 寄存器序列 + 调色板 (8bpp 回退) ----
static void mode13_init(void) {
#define VGAW(p, v) do { __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(v)), "d"((uint16_t)(p))); } while(0)
#define VGAR(p) ({ uint8_t __v; __asm__ volatile("inb %1, %0" : "=a"(__v) : "d"((uint16_t)(p))); __v; })
    VGAW(0x3C2, 0x63);
    VGAW(0x3C4, 0x00); VGAW(0x3C5, 0x03);
    VGAW(0x3C4, 0x01); VGAW(0x3C5, 0x01);
    VGAW(0x3C4, 0x02); VGAW(0x3C5, 0x0F);
    VGAW(0x3C4, 0x03); VGAW(0x3C5, 0x00);
    VGAW(0x3C4, 0x04); VGAW(0x3C5, 0x0E);
    VGAW(0x3CE, 0x00); VGAW(0x3CF, 0x00);
    VGAW(0x3CE, 0x01); VGAW(0x3CF, 0x00);
    VGAW(0x3CE, 0x02); VGAW(0x3CF, 0x00);
    VGAW(0x3CE, 0x03); VGAW(0x3CF, 0x00);
    VGAW(0x3CE, 0x04); VGAW(0x3CF, 0x00);
    VGAW(0x3CE, 0x05); VGAW(0x3CF, 0x40);
    VGAW(0x3CE, 0x06); VGAW(0x3CF, 0x05);
    VGAW(0x3CE, 0x07); VGAW(0x3CF, 0x0F);
    VGAW(0x3CE, 0x08); VGAW(0x3CF, 0xFF);
    // 先解锁 CRTC 保护寄存器 (bit 7 of index 0x11)
    VGAW(0x3D4, 0x11);
    uint8_t cr11 = VGAR(0x3D5);
    VGAW(0x3D5, cr11 & 0x7F);
    const uint8_t crtc[][2] = {
        {0x00,0x5F},{0x01,0x4F},{0x02,0x50},{0x03,0x82},{0x04,0x54},{0x05,0x80},
        {0x06,0x0B},{0x07,0x3E},{0x08,0x00},{0x09,0x40},{0x0A,0x00},{0x0B,0x00},
        {0x0C,0x00},{0x0D,0x00},{0x0E,0x00},{0x0F,0x00},{0x10,0x0A},{0x11,0x0E},
        {0x12,0x00},{0x13,0x28},{0x14,0x40},{0x15,0x00},{0x16,0xEA},{0x17,0x0C},
        {0x18,0x00}};
    for (int i = 0; i < 25; i++) { VGAW(0x3D4, crtc[i][0]); VGAW(0x3D5, crtc[i][1]); }
    // 属性控制器: 必须 inb(0x3DA) 复位索引状态, 然后再写索引/数据
    (void)VGAR(0x3DA);
    // 0x00~0x0F 必须设为恒等映射 (0x00..0x0F), 否则 256 色模式颜色会被错映射
    for (int i = 0; i < 16; i++) { VGAW(0x3C0, (uint8_t)i); VGAW(0x3C0, (uint8_t)i); }
    VGAW(0x3C0, 0x30); VGAW(0x3C0, 0x41);
    VGAW(0x3C0, 0x31); VGAW(0x3C0, 0x00);
    VGAW(0x3C0, 0x32); VGAW(0x3C0, 0x0F);
    VGAW(0x3C0, 0x33); VGAW(0x3C0, 0x00);
    VGAW(0x3C0, 0x34); VGAW(0x3C0, 0x00);
    VGAW(0x3C0, 0x20);   // 解锁/启用属性控制器
#undef VGAW
#undef VGAR

    // 编程 DAC 调色板 (6-bit): 先写索引到 0x3C8, 再按 R,G,B 顺序写 0x3C9
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xFF), "d"((uint16_t)0x3C6)); // 掩码全 1
    for (int i = 0; i < GFX_COLORS; i++) {
        uint32_t c = gfx_palette[i];
        uint8_t r = (c >> 16) & 0x3F, g = (c >> 8) & 0x3F, b = c & 0x3F;
        __asm__ volatile("outb %0, %1" :: "a"((uint8_t)i), "d"((uint16_t)0x3C8));
        __asm__ volatile("outb %0, %1" :: "a"(r), "d"((uint16_t)0x3C9));
        __asm__ volatile("outb %0, %1" :: "a"(g), "d"((uint16_t)0x3C9));
        __asm__ volatile("outb %0, %1" :: "a"(b), "d"((uint16_t)0x3C9));
    }
}

static void gfx_setup_backbuffer(void) {
    g_back = (uint8_t*)kmalloc((size_t)g_h * g_pitch);
    // 清屏为黑
    for (size_t i = 0; i < (size_t)g_h * g_pitch; i++) g_back[i] = 0;
}

void gfx_init_mode13(void) {
    g_lfb = 0;
    g_w = 320; g_h = 200; g_pitch = 320;
    g_bpp = 8; g_bpp_bytes = 1;
    g_rshift = 0; g_gshift = 0; g_bshift = 0; g_rsize = 8;
    mode13_init();                       // 编程 VGA + DAC 调色板 (6-bit)
    g_front = (void*)0xA0000UL;
    gfx_setup_backbuffer();
}

// ---- VBE 线性帧缓冲信息块 (loader.asm 实模式写入物理 0x6400) ----
#define VBE_MAGIC 0x32454256UL          // 'VBE2'
typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;       // 每行字节数
    uint32_t bpp;
    uint32_t format;      // 0=RGBX (R在高字节), 1=BGRX (B在高字节)
    uint64_t fb_addr;     // LFB 物理地址
} vbe_info_t;

// 激活 VBE 线性帧缓冲路径 (32bpp 真彩, 双缓冲在内核堆)
static void gfx_init_lfb(const vbe_info_t* vi) {
    g_lfb = 1;
    g_w = (int)vi->width;
    g_h = (int)vi->height;
    g_pitch = (int)vi->pitch;
    g_bpp = (int)vi->bpp;
    g_bpp_bytes = (int)(vi->bpp / 8);
    g_rsize = 8;
    // 32bpp 像素布局: RGBX -> R<<16|G<<8|B;  BGRX -> B<<16|G<<8|R
    if (vi->format == 0) {              // RGBX
        g_rshift = 16; g_gshift = 8; g_bshift = 0;
    } else {                            // BGRX
        g_rshift = 0;  g_gshift = 8; g_bshift = 16;
    }
    g_front = (void*)(uintptr_t)vi->fb_addr;
    g_font_scale = 2;                   // LFB 32bpp 下 2x 放大字体 (8x8→16x16) 提升清晰度
    gfx_setup_backbuffer();
}

// 串口调试输出 (COM1)
void gfx_ser_putc(char c) {
    while ((inb(0x3FD) & 0x20) == 0) {}
    outb(0x3F8, (uint8_t)c);
}
void gfx_ser_str(const char* s) { while (*s) gfx_ser_putc(*s++); }
static void gfx_ser_hex(uint32_t v) {
    gfx_ser_putc('0'); gfx_ser_putc('x');
    for (int i = 28; i >= 0; i -= 4) gfx_ser_putc("0123456789ABCDEF"[(v >> i) & 0xF]);
}

void gfx_init(void) {
    // 探测 VBE LFB 信息 (loader.asm vbe_setup 写入 0x6400, magic 'VBE2')。
    // 严格校验 magic + 几何, 避免未初始化垃圾值误入 LFB 路径。
    const vbe_info_t* vi = (const vbe_info_t*)(uintptr_t)VBE_INFO;
    gfx_ser_str("[gfx] VBE magic="); gfx_ser_hex(vi->magic);
    gfx_ser_str(" w="); gfx_ser_hex(vi->width);
    gfx_ser_str(" h="); gfx_ser_hex(vi->height);
    gfx_ser_str(" bpp="); gfx_ser_hex(vi->bpp);
    gfx_ser_str(" pitch="); gfx_ser_hex(vi->pitch);
    gfx_ser_str(" fb="); gfx_ser_hex((uint32_t)vi->fb_addr);
    gfx_ser_str("\r\n");
    if (vi->magic == VBE_MAGIC && vi->fb_addr != 0 &&
        vi->width >= 320 && vi->height >= 200 &&
        vi->bpp == 32 && vi->pitch >= vi->width * 4) {
        gfx_ser_str("[gfx] activating LFB path\r\n");
        gfx_init_lfb(vi);
        gfx_ser_str("[gfx] LFB ready: w="); gfx_ser_hex(g_w);
        gfx_ser_str(" h="); gfx_ser_hex(g_h);
        gfx_ser_str(" pitch="); gfx_ser_hex(g_pitch);
        gfx_ser_str(" bpp_bytes="); gfx_ser_hex(g_bpp_bytes);
        gfx_ser_str(" back="); gfx_ser_hex((uint32_t)(uintptr_t)g_back);
        gfx_ser_str(" front="); gfx_ser_hex((uint32_t)(uintptr_t)g_front);
        gfx_ser_str("\r\n");
        return;
    }
    gfx_ser_str("[gfx] falling back to mode13h\r\n");
    // 回退 mode13h 320x200x256 (QEMU/VMware 均安全)
    gfx_init_mode13();
}

int gfx_width(void)  { return g_w; }
int gfx_height(void) { return g_h; }
int gfx_is_lfb(void) { return g_lfb; }

// ---- 真彩接口 ----
void gfx_clear_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t px = pack_rgb(r, g, b);
    size_t n = (size_t)g_h * g_pitch;
    if (g_bpp_bytes == 4) { uint32_t* p = (uint32_t*)g_back; for (size_t i = 0; i < n/4; i++) p[i] = px; }
    else if (g_bpp_bytes == 2) { uint16_t* p = (uint16_t*)g_back; uint16_t v=(uint16_t)px; for (size_t i=0;i<n/2;i++) p[i]=v; }
    else { for (size_t i = 0; i < n; i++) g_back[i] = (uint8_t)px; }
}
void gfx_pixel_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    put_px(x, y, pack_rgb(r, g, b));
}
void gfx_fill_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= g_w) x1 = g_w - 1; if (y1 >= g_h) y1 = g_h - 1;
    if (x1 < x0 || y1 < y0) return;
    uint32_t px = pack_rgb(r, g, b);
    for (int y = y0; y <= y1; y++) {
        uint8_t* row = g_back + (size_t)y * g_pitch;
        for (int x = x0; x <= x1; x++) {
            if (g_bpp_bytes == 4) *(uint32_t*)&row[x*4] = px;
            else if (g_bpp_bytes == 2) *(uint16_t*)&row[x*2] = (uint16_t)px;
            else row[x] = (uint8_t)px;
        }
    }
}
void gfx_rect_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    gfx_fill_rgb(x0, y0, x1, y0, r, g, b);
    gfx_fill_rgb(x0, y1, x1, y1, r, g, b);
    gfx_fill_rgb(x0, y0, x0, y1, r, g, b);
    gfx_fill_rgb(x1, y0, x1, y1, r, g, b);
}
int gfx_text_w(const char* s) {
    int n = 0; while (*s) { n++; s++; } return n * 8 * g_font_scale;
}
void gfx_text_rgb(int x, int y, const char* s,
                  uint8_t fr, uint8_t fg, uint8_t fb,
                  uint8_t br, uint8_t bg, uint8_t bb) {
    uint32_t fgc = pack_rgb(fr, fg, fb);
    uint32_t bgc = pack_rgb(br, bg, bb);
    const uint8_t* font = (const uint8_t*)g_font8x8;   // 内置字体, 按 (ch-0x20)*8 索引
    int sc = g_font_scale;
    while (*s) {
        uint8_t ch = (uint8_t)*s;
        if (ch >= 128) ch = '?';
        if (ch < 0x20) ch = 0x20;                      // 控制字符 -> 空白
        const uint8_t* gl = &font[(ch - 0x20) * 8];
        // font8x8.h: petme128 每字节一列, bit0=最上; sc x sc 块渲染
        for (int col = 0; col < 8; col++) {
            uint8_t bits = gl[col];
            for (int row = 0; row < 8; row++) {
                int on = (bits >> row) & 1;
                uint32_t c = on ? fgc : bgc;
                int px = x + col * sc, py = y + row * sc;
                for (int dy = 0; dy < sc; dy++)
                    for (int dx = 0; dx < sc; dx++)
                        put_px(px + dx, py + dy, c);
            }
        }
        x += 8 * sc; s++;
    }
}

// ---- 8bpp 索引接口 (vga.c 兼容, COL_* 索引) ----
// LFB (32bpp) 路径下, 索引经 gfx_palette 转真彩色; mode13h (8bpp) 直接写索引。
static inline void idx_to_rgb(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint32_t c = (idx < GFX_COLORS) ? gfx_palette[idx] : 0;
    // gfx_palette 存 6-bit DAC 值 (0..63), LFB 32bpp 需 8-bit (0..255)
    // 与 GOP 路径 gop_dac8 同公式, 否则整体暗 4 倍
    uint32_t r6 = (c >> 16) & 0x3F, g6 = (c >> 8) & 0x3F, b6 = c & 0x3F;
    *r = (r6 * 255u + 31u) / 63u;
    *g = (g6 * 255u + 31u) / 63u;
    *b = (b6 * 255u + 31u) / 63u;
}
void gfx_clear_idx(uint8_t idx) {
    if (g_lfb) { uint8_t r, g, b; idx_to_rgb(idx, &r, &g, &b); gfx_clear_rgb(r, g, b); return; }
    size_t n = (size_t)g_h * g_pitch;
    for (size_t i = 0; i < n; i++) g_back[i] = idx;
}
void gfx_pixel_idx(int x, int y, uint8_t idx) {
    if (g_lfb) { uint8_t r, g, b; idx_to_rgb(idx, &r, &g, &b); gfx_pixel_rgb(x, y, r, g, b); return; }
    put_px(x, y, idx);
}
void gfx_fill_idx(int x0, int y0, int x1, int y1, uint8_t idx) {
    if (g_lfb) { uint8_t r, g, b; idx_to_rgb(idx, &r, &g, &b); gfx_fill_rgb(x0, y0, x1, y1, r, g, b); return; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= g_w) x1 = g_w - 1; if (y1 >= g_h) y1 = g_h - 1;
    if (x1 < x0 || y1 < y0) return;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            g_back[y * g_pitch + x] = idx;
}
void gfx_rect_idx(int x0, int y0, int x1, int y1, uint8_t idx) {
    gfx_fill_idx(x0, y0, x1, y0, idx);
    gfx_fill_idx(x0, y1, x1, y1, idx);
    gfx_fill_idx(x0, y0, x0, y1, idx);
    gfx_fill_idx(x1, y0, x1, y1, idx);
}
void gfx_text_idx(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    if (g_lfb) {
        uint8_t fr, fgg, fb, br, bgg, bb;
        idx_to_rgb(fg, &fr, &fgg, &fb); idx_to_rgb(bg, &br, &bgg, &bb);
        gfx_text_rgb(x, y, s, fr, fgg, fb, br, bgg, bb);
        return;
    }
    const uint8_t* font = (const uint8_t*)g_font8x8;   // 内置字体, 按 (ch-0x20)*8 索引
    while (*s) {
        uint8_t ch = (uint8_t)*s;
        if (ch >= 128) ch = '?';
        if (ch < 0x20) ch = 0x20;                      // 控制字符 -> 空白
        const uint8_t* gl = &font[(ch - 0x20) * 8];
        // font8x8.h: petme128 每字节一列, bit0=最上
        for (int col = 0; col < 8; col++) {
            uint8_t bits = gl[col];
            for (int row = 0; row < 8; row++) {
                int on = (bits >> row) & 1;
                put_px(x + col, y + row, on ? fg : bg);
            }
        }
        x += 8; s++;
    }
}

void gfx_flip(void) {
    // 后台 -> 前台, 行级脏拷贝: 逐行比较, 只有相对前台变化了的行才拷贝.
    // (静止桌面与前台完全一致 -> 全程零写屏, 消除闪烁与撕裂;
    //  鼠标指针移动/窗口动画只改写其影响的行 -> 写放大从整屏 64KB 降到几 KB,
    //  VM/TCG 下每帧开销大减, wm 循环更快, 指针移动更稳更跟手.)
    if (!g_back || !g_front || g_w <= 0 || g_h <= 0) return;
    const size_t rb = (size_t)g_pitch;   // 每行字节数 (g_pitch 已是字节跨度)
    uint8_t* f = (uint8_t*)g_front;
    const uint8_t* b = (const uint8_t*)g_back;
    for (int y = 0; y < g_h; y++) {
        uint8_t* r0 = f + (size_t)y * rb;
        const uint8_t* r1 = b + (size_t)y * rb;
        int same = 1;
        for (size_t i = 0; i < rb; i++)
            if (r0[i] != r1[i]) { same = 0; break; }
        if (same) continue;
        for (size_t i = 0; i < rb; i++) r0[i] = r1[i];
    }
}

// ================= UEFI (GOP) 帧缓冲呈现 =================
// UEFI 固件把虚拟显卡置于 GOP 线性帧缓冲模式, legacy VGA (0xA0000/0xB8000)
// 不连接屏幕 —— UEFI 黑屏的根因。UEFI loader (boot/uefi/main.c) 在退出启动服务前
// 把 GOP 信息写入物理 0x6400 (magic 'GFXL', 结构见 boot/uefi/efi.h::gop_info_t),
// 并把帧缓冲映射为 UC。这里把 0xA0000 这个 320x200x8 "mode13h 前端" (vga_*/gfx_*
// 所有绘制最终汇聚于此, gfx_flip() 也把后台缓冲拷到它) 周期性镜像到 GOP 帧缓冲,
// 使 BIOS 与 UEFI 路径共用同一套 8bpp 绘制代码。
// BIOS 路径 0x6400 无 GOP 信息 -> 自动失效 (屏幕直接连 mode13h, 行为不变)。
// 由 PIT 中断 (idt.c::pit_handler) 周期驱动, 内部节流。
#include "idt.h"                 // get_ticks() (节流)

#define GOP_INFO_ADDR   0x6400UL
#define GOP_INFO_MAGIC  0x4C584647UL    // 'GFXL'

typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;      // 每行字节数
    uint32_t bpp;
    uint32_t format;     // GOP PixelFormat: 0=RGBX 1=BGRX
    uint64_t fb_addr;
} gop_info_t;

static int      g_gop_state  = 0;       // 0=未探测 1=有效 -1=不可用(BIOS 路径)
static uint32_t g_gop_w, g_gop_h, g_gop_pitch, g_gop_fmt;
static uint8_t* g_gop_fb;
static uint32_t g_gop_lut[256];
static uint32_t g_gop_last_ms;
// ---- 防闪烁呈现状态 ----
static uint32_t g_gop_scale, g_gop_offx, g_gop_offy;  // 居中缩放几何
static int      g_gop_bg_done;                          // 背景色只铺一次
static uint8_t  g_gop_snap[200 * 320];                  // 上次已上屏的前端快照

// mode13h 调色板 gfx_palette[] 存的是 VGA DAC 6bit 值 (0..63, 与 int10 写 DAC 一致);
// GOP 帧缓冲每通道 8bit (0..255), 直接使用会整体暗 4 倍 (白≈63 灰), 文字/背景对比塌陷
// 成"模糊看不清"。这里把 6bit DAC 分量扩展回 8bit。
static inline uint32_t gop_dac8(uint32_t v) {
    return (v * 255u + 31u) / 63u;
}
static inline uint32_t gop_rgb8(uint32_t dac6) {
    return (gop_dac8((dac6 >> 16) & 0xFFu) << 16) |
           (gop_dac8((dac6 >> 8)  & 0xFFu) << 8)  |
            gop_dac8(dac6 & 0xFFu);
}

void gfx_gop_present(void) {
    if (g_gop_state == 0) {
        const gop_info_t* gi = (const gop_info_t*)(uintptr_t)GOP_INFO_ADDR;
        if (gi->magic != GOP_INFO_MAGIC || gi->fb_addr == 0 ||
            gi->width < 320 || gi->height < 200 || gi->pitch < 320 * 4) {
            g_gop_state = -1;           // 无 GOP: 只探测一次, 之后快速返回
            return;
        }
        g_gop_state  = 1;
        g_gop_w      = gi->width;
        g_gop_h      = gi->height;
        g_gop_pitch  = gi->pitch;
        g_gop_fmt    = gi->format;
        g_gop_fb     = (uint8_t*)(uintptr_t)gi->fb_addr;
        // 整数缩放居中几何, 只算一次
        g_gop_scale  = (g_gop_w / 320 < g_gop_h / 200) ? (g_gop_w / 320) : (g_gop_h / 200);
        if (g_gop_scale < 1) g_gop_scale = 1;
        g_gop_offx   = (g_gop_w - 320 * g_gop_scale) / 2;
        g_gop_offy   = (g_gop_h - 200 * g_gop_scale) / 2;
        g_gop_bg_done = 0;
        g_gop_last_ms = 0;
        // 调色板是 DAC 6bit(0..63), 必须扩展为 8bit 再进 GOP 帧缓冲
        for (int i = 0; i < 256; i++) {
            uint32_t c = (i < GFX_COLORS) ? gfx_palette[i] : 0;
            g_gop_lut[i] = gop_rgb8(c);
        }
    }
    if (g_gop_state != 1) return;

    // 节流 ~8ms (125Hz 检查). 实际写屏只在内容变化行发生, 静止桌面零开销;
    // 比 16ms 更密的采样让鼠标指针/动画移动在 GOP 上更跟手.
    uint32_t now = get_ticks();
    if ((uint32_t)(now - g_gop_last_ms) < 8) return;
    g_gop_last_ms = now;

    const uint8_t* src = (const uint8_t*)0xA0000UL;
    const uint32_t wd  = g_gop_pitch / 4;          // GOP 每行 32-bit 字数
    const uint32_t scale = g_gop_scale;

    // 首次: 整屏铺背景色消除黑边 (只铺一次, 之后不动 -> 不再反复清屏闪烁)
    if (!g_gop_bg_done) {
        g_gop_bg_done = 1;
        uint32_t bg = gop_rgb8(gfx_palette[26]);   // WALL_F (26): 与新壁纸渐变的深色底一致 (DAC6->8bit)
        // GOP PixelFormat: 0=RGBX(低字节=红), 1=BGRX(低字节=蓝). 调色板按 0xRRGGBB 展开,
        // 小端下低字节=蓝, 故仅 RGBX 需要交换 R/B; BGRX 直接写.
        if (g_gop_fmt == 0) bg = ((bg & 0xFFu) << 16) | (bg & 0xFF00u) | (bg >> 16);
        for (uint32_t by = 0; by < g_gop_h; by++) {
            volatile uint32_t* brow = (volatile uint32_t*)(g_gop_fb + (size_t)by * g_gop_pitch);
            for (uint32_t bx = 0; bx < g_gop_w; bx++) brow[bx] = bg;
        }
    }

    // 行级脏检测: 只把 0xA0000 前端相对快照变化了的行放大重绘上屏.
    // (鼠标移动/时钟/窗口动画只改少数行; 完全静止时所有行相同, 全程不碰 GOP fb)
    for (uint32_t y = 0; y < 200; y++) {
        const uint8_t* cur = src + (size_t)y * 320;
        uint8_t* sn = g_gop_snap + (size_t)y * 320;
        int same = 1;
        for (uint32_t x = 0; x < 320; x++)
            if (sn[x] != cur[x]) { same = 0; break; }
        if (same) continue;
        for (uint32_t x = 0; x < 320; x++) sn[x] = cur[x];   // 快照同步

        volatile uint32_t* d0 = (volatile uint32_t*)(g_gop_fb +
            (size_t)(g_gop_offy + y * scale) * g_gop_pitch) + g_gop_offx;
        for (uint32_t sy = 0; sy < scale; sy++) {
            volatile uint32_t* dst = d0 + (size_t)sy * wd;
            for (uint32_t x = 0; x < 320; x++) {
                uint32_t c = g_gop_lut[cur[x]];
                if (g_gop_fmt == 0) c = ((c & 0xFFu) << 16) | (c & 0xFF00u) | (c >> 16);
                for (uint32_t sx = 0; sx < scale; sx++) dst[x * scale + sx] = c;
            }
        }
    }
}
