// gfx.c - 统一图形核心
//
// 显示模式:
//   - 优先 VBE 线性帧缓冲 (boot.asm 探测, 信息存 0x6400). 32/16bpp 真彩.
//   - 回退 mode13h 320x200x256 (8bpp, 调色板索引).
// 双缓冲: 所有绘制写入 g_back, gfx_flip() 整屏拷贝到前台帧缓冲.
#include "gfx.h"
#include <stdint.h>
#include "vga.h"       // COL_UI_BASE (modern_ui 扩展色索引 160)
#include "kheap.h"
#include "io.h"          // outb/inb (串口调试)
// UEFI 无 int10h 时 0xB0000 (VGA/MMIO 窗口) 读回的字形不可靠, 改用内核内置字体
// (boot/uefi/font8x8.h 的 g_font8x8[96][8], 按 ch-0x20 索引), BIOS/UEFI 路径一致。
#include "boot/uefi/font8x8.h"

// VBE 信息结构 (boot.asm 写入物理 0x6400)
#define VBE_INFO 0x6400UL
#define FONT_ADDR 0xB0000UL

// GOP 信息结构 (UEFI loader 写入物理 0x6400, 与 VBE 互斥, 二选一).
// 前置声明, 供 gfx_init 的 UEFI 原生路径使用.
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
    0x24365A, // 21 WALL_A：蓝紫极光的亮部
    0x1D2D50, // 22 WALL_B
    0x182646, // 23 WALL_C
    0x131E3A, // 24 WALL_D
    0x0E182E, // 25 WALL_E
    0x09101F, // 26 WALL_F：夜色底部
    0x2D7FF9, // 27 ACCENT：清晰、低饱和的系统蓝
    0x294D82, // 28 ACCENT_SOFT：玻璃上的悬停蓝
    0x383C46, // 29 TASKBAR：深色 Dock / 菜单底
    0x4B5260, // 30 TASK_HI：Dock / 菜单悬停
    0x05070D, // 31 SHADOW：阴影/描边
};

// 现代 UI 柔和扩展色 160..165 (与 vga.h COL_UI_* 同步; 每字节为 6-bit DAC 值 0..63,
// 与 gfx_palette 存法一致, 经 idx_to_rgb 高 6 位展开为 8bit)
// 均为低饱和柔和色: 状态色可区分、大面积背景低饱和、标题/Dock 悬停层次分明。
const uint32_t gfx_palette_ext[GFX_COLORS_EXT] = {
    0x162818, // 160 SOFT_OK  柔和绿 (成功)
    0x2A1A1A, // 161 SOFT_ERR 柔和红 (错误)
    0x2B2518, // 162 SOFT_WARN 柔和琥珀 (警告)
    0x101C2F, // 163 BG_SOFT  夜色蓝灰 (大面积背景)
    0x17233B, // 164 TITLE_SOFT 标题栏深蓝 (聚焦)
    0x202F3F, // 165 DOCK_HI_SOFT Dock 悬停浅蓝
};

// 取调色板真彩色: 索引 <32 查基础表, 160..165 查扩展表, 其余返回 0
static inline uint32_t palette_color(uint8_t idx) {
    if (idx < GFX_COLORS) return gfx_palette[idx];
    if (idx >= COL_UI_BASE && idx < COL_UI_BASE + GFX_COLORS_EXT)
        return gfx_palette_ext[idx - COL_UI_BASE];
    return 0;
}

static int      g_lfb = 0;
static int      g_w = 0, g_h = 0, g_pitch = 0, g_bpp = 0, g_bpp_bytes = 0;
static uint8_t  g_rshift = 16, g_gshift = 8, g_bshift = 0, g_rsize = 8;
static uint8_t* g_back = 0;        // 后台缓冲 (内核堆)
static void*    g_front = 0;       // 前台物理帧缓冲
static int      g_font_scale = 1;  // 字体缩放 (LFB=2 提升清晰度, mode13h=1)

// 实际后台缓冲几何 (UEFI 原生路径下 != 逻辑分辨率):
// 逻辑分辨率恒为 320x200 (apps 用 gfx_width/height 不变), 实际缓冲按 GOP 原生尺寸,
// 由 g_scale 把逻辑坐标放大渲染进去 -> 原生分辨率 + 抗锯齿字体, apps 零改动.
static int g_buf_w = 0, g_buf_h = 0, g_buf_pitch = 0;
static int g_scale = 1;            // 逻辑像素 -> 原生像素倍数 (UEFI 原生路径 >1)
static int g_off_x = 0, g_off_y = 0; // 逻辑坐标系在原生缓冲中的居中偏移
static int g_native = 0;           // 1 = UEFI GOP 原生 32bpp 路径 (present 不再镜像放大)

int gfx_font_scale(void) { return g_font_scale; }
int gfx_scale(void)      { return g_scale; }

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

// 写单个"原生"像素 (不做逻辑->原生缩放, 供抗锯齿字形直接调用)
static inline void put_px_real(int rx, int ry, uint32_t px) {
    rx += g_off_x; ry += g_off_y;
    if (rx < 0 || ry < 0 || rx >= g_buf_w || ry >= g_buf_h) return;
    uint8_t* p = g_back + (size_t)ry * g_buf_pitch + (size_t)rx * g_bpp_bytes;
    if (g_bpp_bytes == 4)      *(uint32_t*)p = px;
    else if (g_bpp_bytes == 2) *(uint16_t*)p = (uint16_t)px;
    else                       *p = (uint8_t)px;
}

// 写逻辑像素: 自动按 g_scale 放大成 g_scale x g_scale 的原生像素块.
// 所有图元都经此出口 -> 形状在原生分辨率下锐利; mode13h 时 g_scale=1 行为不变.
static inline void put_px(int x, int y, uint32_t px) {
    int rx = x * g_scale + g_off_x, ry = y * g_scale + g_off_y;
    for (int dy = 0; dy < g_scale; dy++) {
        int Y = ry + dy;
        if (Y < 0 || Y >= g_buf_h) continue;
        uint8_t* row = g_back + (size_t)Y * g_buf_pitch;
        for (int dx = 0; dx < g_scale; dx++) {
            int X = rx + dx;
            if (X < 0 || X >= g_buf_w) continue;
            uint8_t* p = row + (size_t)X * g_bpp_bytes;
            if (g_bpp_bytes == 4)      *(uint32_t*)p = px;
            else if (g_bpp_bytes == 2) *(uint16_t*)p = (uint16_t)px;
            else                       *p = (uint8_t)px;
        }
    }
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
    g_back = (uint8_t*)kmalloc((size_t)g_buf_h * g_buf_pitch);
    if (!g_back) return;                 // 分配失败, 调用方检测 g_back==NULL
    for (size_t i = 0; i < (size_t)g_buf_h * g_buf_pitch; i++) g_back[i] = 0;
}

void gfx_init_mode13(void) {
    g_lfb = 0; g_native = 0; g_scale = 1; g_off_x = 0; g_off_y = 0;
    g_w = 320; g_h = 200; g_pitch = 320;
    g_buf_w = 320; g_buf_h = 200; g_buf_pitch = 320;
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

// 串口调试输出 (前置声明, 定义在下方)
void gfx_ser_str(const char* s);
static void gfx_ser_hex(uint32_t v);

// 字体缩放因子: 按屏高分档返回, 保证高分辨率下文本可读性
static int compute_font_scale(int width, int height) {
    (void)width;
    if (height >= 1080) return 4;       // 8×4=32px, 1080/32≈33 行
    if (height >= 800)  return 3;       // 8×3=24px, 800/24≈33 行
    if (height >= 480)  return 2;       // 8×2=16px, 480/16=30 行
    return 1;                           // 320x200 等, 8px 原始
}

// 激活 VBE 线性帧缓冲路径 (32bpp 真彩, 双缓冲在内核堆)
// 返回 0=成功, -1=失败 (调用方回退 GOP/mode13h)
static int gfx_init_lfb(const vbe_info_t* vi) {
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
    g_native = 0; g_scale = 1; g_off_x = 0; g_off_y = 0;
    g_buf_w = g_w; g_buf_h = g_h; g_buf_pitch = g_pitch;   // 原生/逻辑重合
    g_font_scale = compute_font_scale(g_w, g_h);
    gfx_setup_backbuffer();
    if (!g_back) {
        gfx_ser_str("[gfx] LFB backbuffer alloc failed, size=");
        gfx_ser_hex((uint32_t)((size_t)g_buf_h * g_buf_pitch));
        gfx_ser_str("\r\n");
        return -1;
    }
    return 0;
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
        vi->width >= 640 && vi->height >= 480 &&
        vi->bpp == 32 && vi->pitch >= vi->width * 4) {
        gfx_ser_str("[gfx] activating LFB path\r\n");
        if (gfx_init_lfb(vi) == 0) {
            gfx_ser_str("[gfx] LFB ready: w="); gfx_ser_hex(g_w);
            gfx_ser_str(" h="); gfx_ser_hex(g_h);
            gfx_ser_str(" pitch="); gfx_ser_hex(g_pitch);
            gfx_ser_str(" bpp_bytes="); gfx_ser_hex(g_bpp_bytes);
            gfx_ser_str(" back="); gfx_ser_hex((uint32_t)(uintptr_t)g_back);
            gfx_ser_str(" front="); gfx_ser_hex((uint32_t)(uintptr_t)g_front);
            gfx_ser_str("\r\n");
            return;
        }
        gfx_ser_str("[gfx] LFB init failed, trying next path\r\n");
    }

    // UEFI GOP 原生路径: 0x6400 也可能是 GOP 信息 (magic 'GFXL').
    // 直接以原生 32bpp 初始化 LFB, 逻辑分辨率仍 320x200, 绘制由 g_scale 放大 ->
    // 原生分辨率 + 抗锯齿字体, 且所有 wm.c/app 代码零改动. 失败时回退 mode13h.
    {
        const gop_info_t* gi = (const gop_info_t*)(uintptr_t)GOP_INFO_ADDR;
        if (gi->magic == GOP_INFO_MAGIC && gi->fb_addr != 0 &&
            gi->width >= 320 && gi->height >= 200 && gi->pitch >= gi->width * 4) {
            int s = (gi->width / 320 < gi->height / 200) ? (gi->width / 320) : (gi->height / 200);
            if (s < 1) s = 1;
            gfx_ser_str("[gfx] activating GOP native path, scale="); gfx_ser_hex((uint32_t)s); gfx_ser_str("\r\n");
            g_native = 1;
            g_lfb = 1;
            g_w = 320; g_h = 200;                 // 逻辑分辨率 (apps 不变)
            g_buf_w = (int)gi->width;
            g_buf_h = (int)gi->height;
            g_buf_pitch = (int)gi->pitch;
            g_bpp = 32; g_bpp_bytes = 4; g_rsize = 8;
            // 与旧 gfx_gop_present 一致: fmt==0(GOP RGBX) 需 R/B 交换 -> 存 0x00BBGGRR (LE byte0=R)
            if (gi->format == 0) { g_rshift = 0;  g_gshift = 8; g_bshift = 16; }  // RGBX
            else                 { g_rshift = 16; g_gshift = 8; g_bshift = 0;  }  // BGRX
            g_front = (void*)(uintptr_t)gi->fb_addr;
            g_scale = s;
            g_font_scale = s;                      // 供 gfx_text_w / vga 间距对齐
            g_off_x = (g_buf_w - g_w * s) / 2;
            g_off_y = (g_buf_h - g_h * s) / 2;
            gfx_setup_backbuffer();
            return;
        }
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
    size_t n = (size_t)g_buf_h * g_buf_pitch;
    if (g_bpp_bytes == 4) { uint32_t* p = (uint32_t*)g_back; for (size_t i = 0; i < n/4; i++) p[i] = px; }
    else if (g_bpp_bytes == 2) { uint16_t* p = (uint16_t*)g_back; uint16_t v=(uint16_t)px; for (size_t i=0;i<n/2;i++) p[i]=v; }
    else { for (size_t i = 0; i < n; i++) g_back[i] = (uint8_t)px; }
}
void gfx_pixel_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    put_px(x, y, pack_rgb(r, g, b));
}
void gfx_fill_rgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    // 先按逻辑坐标裁剪, 再按 g_scale 放大成真实矩形填充实缓冲.
    // mode13h/VBE-LFB 时 g_scale=1 且 g_buf_* == g_*/g_pitch, 行为与旧代码一致.
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= g_w) x1 = g_w - 1; if (y1 >= g_h) y1 = g_h - 1;
    if (x1 < x0 || y1 < y0) return;
    uint32_t px = pack_rgb(r, g, b);
    int sc = g_scale;
    int rx0 = x0 * sc + g_off_x, ry0 = y0 * sc + g_off_y;
    int rx1 = (x1 + 1) * sc - 1 + g_off_x, ry1 = (y1 + 1) * sc - 1 + g_off_y;
    if (rx0 < 0) rx0 = 0; if (ry0 < 0) ry0 = 0;
    if (rx1 >= g_buf_w) rx1 = g_buf_w - 1;
    if (ry1 >= g_buf_h) ry1 = g_buf_h - 1;
    for (int y = ry0; y <= ry1; y++) {
        uint8_t* row = g_back + (size_t)y * g_buf_pitch;
        for (int x = rx0; x <= rx1; x++) {
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
    int n = 0; while (*s) { n++; s++; } return n * 8;   // 逻辑宽度 (g_scale 由绘制内部完成)
}
// 普通(非抗锯齿)字形: 每个逻辑像素一个 8x8 块 (mode13h / scale=1 时使用)
static void gfx_glyph_plain(int x, int y, uint8_t ch,
                            uint8_t fr, uint8_t fg, uint8_t fb,
                            uint8_t br, uint8_t bg, uint8_t bb) {
    const uint8_t* font = (const uint8_t*)g_font8x8;
    if (ch >= 128) ch = '?';
    if (ch < 0x20) ch = 0x20;
    const uint8_t* gl = &font[(ch - 0x20) * 8];
    uint32_t fgc = pack_rgb(fr, fg, fb);
    uint32_t bgc = pack_rgb(br, bg, bb);
    for (int col = 0; col < 8; col++) {
        uint8_t bits = gl[col];
        for (int row = 0; row < 8; row++)
            put_px(x + col, y + row, ((bits >> row) & 1) ? fgc : bgc);
    }
}

// 抗锯齿字形: 把 8x8 位图按 g_scale 放大, 用定点双线性覆盖混合 fg/bg -> 平滑边缘.
// 直接写"原生"像素 (put_px_real), 绕过 put_px 的逻辑->原生块放大, 以获得子像素精度.
static void gfx_glyph_aa(int x, int y, uint8_t ch,
                         uint8_t fr, uint8_t fg, uint8_t fb,
                         uint8_t br, uint8_t bg, uint8_t bb) {
    const uint8_t* font = (const uint8_t*)g_font8x8;
    if (ch >= 128) ch = '?';
    if (ch < 0x20) ch = 0x20;
    const uint8_t* gl = &font[(ch - 0x20) * 8];
    int sc = g_scale;
    int ox = x * sc, oy = y * sc;
    for (int ry = 0; ry < 8 * sc; ry++) {
        int pys = (ry << 16) / sc;             // 16.16 逻辑 y 坐标
        int y0 = pys >> 16; if (y0 > 7) y0 = 7;
        int y1 = y0 + 1;    if (y1 > 7) y1 = 7;
        int ty = pys & 0xFFFF;
        for (int rx = 0; rx < 8 * sc; rx++) {
            int pxs = (rx << 16) / sc;
            int x0 = pxs >> 16; if (x0 > 7) x0 = 7;
            int x1 = x0 + 1;    if (x1 > 7) x1 = 7;
            int tx = pxs & 0xFFFF;
            int b00 = (gl[x0] >> y0) & 1, b10 = (gl[x1] >> y0) & 1;
            int b01 = (gl[x0] >> y1) & 1, b11 = (gl[x1] >> y1) & 1;
            int v00 = b00 << 16, v10 = b10 << 16, v01 = b01 << 16, v11 = b11 << 16;
            int top = v00 + (((v10 - v00) * tx) >> 16);
            int bot = v01 + (((v11 - v01) * tx) >> 16);
            int cov = top + (((bot - top) * ty) >> 16);   // 0..65535 覆盖度
            uint8_t R = (uint8_t)(br + (((int)fr - br) * cov >> 16));
            uint8_t G = (uint8_t)(bg + (((int)fg - bg) * cov >> 16));
            uint8_t B = (uint8_t)(bb + (((int)fb - bb) * cov >> 16));
            put_px_real(ox + rx, oy + ry, pack_rgb(R, G, B));
        }
    }
}

// 导出: 供 vga.c 文本路径在原生模式下画抗锯齿字形 (参数为 8bit RGB)
void gfx_draw_char_aa(int x, int y, char ch,
                      uint8_t fr, uint8_t fg, uint8_t fb,
                      uint8_t br, uint8_t bg, uint8_t bb) {
    if (g_scale > 1) gfx_glyph_aa(x, y, (uint8_t)ch, fr, fg, fb, br, bg, bb);
    else             gfx_glyph_plain(x, y, (uint8_t)ch, fr, fg, fb, br, bg, bb);
}

// 导出: CJK 16x16 位图抗锯齿放大 (供 cjk.c 在原生模式下调用)
void gfx_draw_cjk_aa(int x, int y, const uint16_t* rows,
                     uint8_t fr, uint8_t fg, uint8_t fb,
                     uint8_t br, uint8_t bg, uint8_t bb) {
    int sc = g_scale;
    int ox = x * sc, oy = y * sc;   // put_px_real 内部再加 g_off_x/y 居中偏移
    for (int ry = 0; ry < 16 * sc; ry++) {
        int pys = (ry << 16) / sc;
        int y0 = pys >> 16; if (y0 > 15) y0 = 15;
        int y1 = y0 + 1;    if (y1 > 15) y1 = 15;
        int ty = pys & 0xFFFF;
        for (int rx = 0; rx < 16 * sc; rx++) {
            int pxs = (rx << 16) / sc;
            int x0 = pxs >> 16; if (x0 > 15) x0 = 15;
            int x1 = x0 + 1;    if (x1 > 15) x1 = 15;
            int tx = pxs & 0xFFFF;
            int b00 = (rows[y0] >> (15 - x0)) & 1;
            int b10 = (rows[y0] >> (15 - x1)) & 1;
            int b01 = (rows[y1] >> (15 - x0)) & 1;
            int b11 = (rows[y1] >> (15 - x1)) & 1;
            int v00 = b00 << 16, v10 = b10 << 16, v01 = b01 << 16, v11 = b11 << 16;
            int top = v00 + (((v10 - v00) * tx) >> 16);
            int bot = v01 + (((v11 - v01) * tx) >> 16);
            int cov = top + (((bot - top) * ty) >> 16);
            uint8_t R = (uint8_t)(br + (((int)fr - br) * cov >> 16));
            uint8_t G = (uint8_t)(bg + (((int)fg - bg) * cov >> 16));
            uint8_t B = (uint8_t)(bb + (((int)fb - bb) * cov >> 16));
            put_px_real(ox + rx, oy + ry, pack_rgb(R, G, B));
        }
    }
}

// 导出: 调色板索引 -> 8bit RGB (供 vga.c 转换 COL_* 字体色)
void gfx_idx_rgb(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint32_t c = palette_color(idx);
    uint32_t r6 = (c >> 16) & 0x3F, g6 = (c >> 8) & 0x3F, b6 = c & 0x3F;
    *r = (uint8_t)((r6 * 255u + 31u) / 63u);
    *g = (uint8_t)((g6 * 255u + 31u) / 63u);
    *b = (uint8_t)((b6 * 255u + 31u) / 63u);
}

void gfx_text_rgb(int x, int y, const char* s,
                  uint8_t fr, uint8_t fg, uint8_t fb,
                  uint8_t br, uint8_t bg, uint8_t bb) {
    int sc = g_scale;
    while (*s) {
        uint8_t ch = (uint8_t)*s;
        if (sc > 1) gfx_glyph_aa(x, y, ch, fr, fg, fb, br, bg, bb);
        else         gfx_glyph_plain(x, y, ch, fr, fg, fb, br, bg, bb);
        x += 8; s++;   // 逻辑步长 8; 真实间距由 g_scale 在字形内完成 (8*sc)
    }
}

// ---- 8bpp 索引接口 (vga.c 兼容, COL_* 索引) ----
// LFB (32bpp) 路径下, 索引经调色板 (基础+扩展) 转真彩色; mode13h (8bpp) 直接写索引。
static inline void idx_to_rgb(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint32_t c = palette_color(idx);
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

void gfx_set_palette_rgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (g_lfb) return;
    // VGA DAC 接收 6-bit 分量；系统 UI 固定使用 0..31，壁纸使用 32..159。
    __asm__ volatile("outb %0, %1" :: "a"(index), "d"((uint16_t)0x3C8));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(r >> 2)), "d"((uint16_t)0x3C9));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(g >> 2)), "d"((uint16_t)0x3C9));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)(b >> 2)), "d"((uint16_t)0x3C9));
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

// ---- 圆角矩形原语 (ui_polish) ----
// 半径钳制: r = min(r, min(w,h)/4), 保证小控件不畸形; r<=0 退化为直角原语.
static int round_r_clamp(int w, int h, int r) {
    int m = w < h ? w : h;
    int cap = m / 4;
    if (r > cap) r = cap;
    if (r < 0) r = 0;
    return r;
}

void gfx_fill_round_idx(int x0, int y0, int x1, int y1, int r, uint8_t idx) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    r = round_r_clamp(w, h, r);
    if (r <= 0) { gfx_fill_idx(x0, y0, x1, y1, idx); return; }

    // 主体: 顶部横条 + 中部全宽 + 底部横条 (避开四角 1/4 圆)
    gfx_fill_idx(x0 + r, y0, x1 - r, y1, idx);
    gfx_fill_idx(x0, y0 + r, x1, y1 - r, idx);

    // 四角 1/4 圆: 距角内圆心 <= r 的像素填充
    int cr = r;                       // 角内圆半径
    int cxx = x0 + cr, cyy = y0 + cr; // 左上圆心 (角内)
    for (int y = y0; y < cyy; y++)
        for (int x = x0; x < cxx; x++)
            if ((cxx - x) * (cxx - x) + (cyy - y) * (cyy - y) <= cr * cr)
                gfx_pixel_idx(x, y, idx);
    cxx = x1 - cr + 1; cyy = y0 + cr;  // 右上圆心
    for (int y = y0; y < cyy; y++)
        for (int x = cxx; x <= x1; x++)
            if ((x - cxx) * (x - cxx) + (cyy - y) * (cyy - y) <= cr * cr)
                gfx_pixel_idx(x, y, idx);
    cxx = x0 + cr; cyy = y1 - cr + 1;  // 左下圆心
    for (int y = cyy; y <= y1; y++)
        for (int x = x0; x < cxx; x++)
            if ((cxx - x) * (cxx - x) + (y - cyy) * (y - cyy) <= cr * cr)
                gfx_pixel_idx(x, y, idx);
    cxx = x1 - cr + 1; cyy = y1 - cr + 1; // 右下圆心
    for (int y = cyy; y <= y1; y++)
        for (int x = cxx; x <= x1; x++)
            if ((x - cxx) * (x - cxx) + (y - cyy) * (y - cyy) <= cr * cr)
                gfx_pixel_idx(x, y, idx);
}

void gfx_round_rect_idx(int x0, int y0, int x1, int y1, int r, uint8_t idx) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    r = round_r_clamp(w, h, r);
    if (r <= 0) { gfx_rect_idx(x0, y0, x1, y1, idx); return; }

    // 四边直线
    gfx_fill_idx(x0 + r, y0, x1 - r, y0, idx);
    gfx_fill_idx(x0 + r, y1, x1 - r, y1, idx);
    gfx_fill_idx(x0, y0 + r, x0, y1 - r, idx);
    gfx_fill_idx(x1, y0 + r, x1, y1 - r, idx);

    // 四角圆弧像素
    int cr = r;
    int cxx = x0 + cr, cyy = y0 + cr;
    for (int y = y0; y <= cyy; y++)
        for (int x = x0; x <= cxx; x++) {
            int dx = cxx - x, dy = cyy - y;
            if (dx * dx + dy * dy <= cr * cr + cr &&
                dx * dx + dy * dy >= (cr - 1) * (cr - 1))
                gfx_pixel_idx(x, y, idx);
        }
    cxx = x1 - cr + 1; cyy = y0 + cr;
    for (int y = y0; y <= cyy; y++)
        for (int x = cxx; x <= x1; x++) {
            int dx = x - cxx, dy = cyy - y;
            if (dx * dx + dy * dy <= cr * cr + cr &&
                dx * dx + dy * dy >= (cr - 1) * (cr - 1))
                gfx_pixel_idx(x, y, idx);
        }
    cxx = x0 + cr; cyy = y1 - cr + 1;
    for (int y = cyy; y <= y1; y++)
        for (int x = x0; x <= cxx; x++) {
            int dx = cxx - x, dy = y - cyy;
            if (dx * dx + dy * dy <= cr * cr + cr &&
                dx * dx + dy * dy >= (cr - 1) * (cr - 1))
                gfx_pixel_idx(x, y, idx);
        }
    cxx = x1 - cr + 1; cyy = y1 - cr + 1;
    for (int y = cyy; y <= y1; y++)
        for (int x = cxx; x <= x1; x++) {
            int dx = x - cxx, dy = y - cyy;
            if (dx * dx + dy * dy <= cr * cr + cr &&
                dx * dx + dy * dy >= (cr - 1) * (cr - 1))
                gfx_pixel_idx(x, y, idx);
        }
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

// ---- 现代 UI 抗锯齿与渐变原语 (modern_ui) ----
// AA 计算锁定图元边缘 1px; 8bpp 路径退化为索引原语 (无逐像素浮点/插值).

// RGB -> 最近调色板索引 (供 8bpp 退化, 每原语仅调一次开销极低)
static uint8_t rgb_to_idx(uint8_t r, uint8_t g, uint8_t b) {
    int best = 0, best_d = 0x7FFFFFFF;
    for (int i = 0; i < GFX_COLORS; i++) {
        uint32_t c = gfx_palette[i];
        int pr = (int)((c >> 16) & 0x3F) * 255 / 63;
        int pg = (int)((c >> 8) & 0x3F) * 255 / 63;
        int pb = (int)(c & 0x3F) * 255 / 63;
        int dr = (int)r - pr, dg = (int)g - pg, db = (int)b - pb;
        int d = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return (uint8_t)best;
}

// 原生像素覆盖混合: 读后台缓冲当前像素, 按 cov (0..256) 混合前景色写回
static inline void alpha_blend_native(int rx, int ry, uint8_t r, uint8_t g, uint8_t b, int cov) {
    if (rx < 0 || ry < 0 || rx >= g_buf_w || ry >= g_buf_h) return;
    uint8_t* p = g_back + (size_t)ry * g_buf_pitch + (size_t)rx * g_bpp_bytes;
    uint32_t cur;
    if (g_bpp_bytes == 4) cur = *(uint32_t*)p;
    else if (g_bpp_bytes == 2) cur = *(uint16_t*)p;
    else cur = *p;
    uint8_t cr = (uint8_t)((cur >> g_rshift) & 0xFF);
    uint8_t cg = (uint8_t)((cur >> g_gshift) & 0xFF);
    uint8_t cb = (uint8_t)((cur >> g_bshift) & 0xFF);
    uint8_t R = (uint8_t)(cr + (((int)r - cr) * cov >> 8));
    uint8_t G = (uint8_t)(cg + (((int)g - cg) * cov >> 8));
    uint8_t B = (uint8_t)(cb + (((int)b - cb) * cov >> 8));
    uint32_t px = pack_rgb(R, G, B);
    if (g_bpp_bytes == 4) *(uint32_t*)p = px;
    else if (g_bpp_bytes == 2) *(uint16_t*)p = (uint16_t)px;
    else *p = (uint8_t)px;
}

// 逻辑像素覆盖混合: 对 g_scale x g_scale 原生块逐像素混合
static inline void blend_px_logical(int x, int y, uint8_t r, uint8_t g, uint8_t b, int cov) {
    int rx = x * g_scale + g_off_x, ry = y * g_scale + g_off_y;
    for (int dy = 0; dy < g_scale; dy++)
        for (int dx = 0; dx < g_scale; dx++)
            alpha_blend_native(rx + dx, ry + dy, r, g, b, cov);
}

// 真彩覆盖混合 (导出): 供 vga_alpha_over 委托
void gfx_alpha_over_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b, int cov) {
    if (!g_lfb) { if (cov >= 128) gfx_pixel_idx(x, y, rgb_to_idx(r, g, b)); return; }
    blend_px_logical(x, y, r, g, b, cov);
}

// 抗锯齿直线 (Wu): 边缘像素按覆盖度混合
void gfx_line_aa(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    if (!g_lfb) {
        uint8_t idx = rgb_to_idx(r, g, b);
        int dx = x1 - x0, dy = y1 - y0;
        int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        if (adx >= ady) {
            int y = y0, err = 0;
            for (int x = x0; x != x1 + sx; x += sx) {
                gfx_pixel_idx(x, y, idx);
                err += ady;
                if (2 * err >= adx) { y += sy; err -= adx; }
            }
        } else {
            int x = x0, err = 0;
            for (int y = y0; y != y1 + sy; y += sy) {
                gfx_pixel_idx(x, y, idx);
                err += adx;
                if (2 * err >= ady) { x += sx; err -= ady; }
            }
        }
        return;
    }
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) { gfx_pixel_rgb(x0, y0, r, g, b); return; }
    if (adx >= ady) {
        int sx = dx < 0 ? -1 : 1;
        int grad = (ady << 16) / adx;
        int yf = y0 << 16;
        for (int i = 0; i <= adx; i++) {
            int x = x0 + i * sx;
            int y_int = yf >> 16;
            int y_frac = yf & 0xFFFF;
            int cov1 = 256 - (y_frac >> 8);
            int cov2 = y_frac >> 8;
            blend_px_logical(x, y_int, r, g, b, cov1);
            if (cov2 > 0) blend_px_logical(x, y_int + 1, r, g, b, cov2);
            yf += grad * (dy < 0 ? -1 : 1);
        }
    } else {
        int sy = dy < 0 ? -1 : 1;
        int grad = (adx << 16) / ady;
        int xf = x0 << 16;
        for (int i = 0; i <= ady; i++) {
            int y = y0 + i * sy;
            int x_int = xf >> 16;
            int x_frac = xf & 0xFFFF;
            int cov1 = 256 - (x_frac >> 8);
            int cov2 = x_frac >> 8;
            blend_px_logical(x_int, y, r, g, b, cov1);
            if (cov2 > 0) blend_px_logical(x_int + 1, y, r, g, b, cov2);
            xf += grad * (dx < 0 ? -1 : 1);
        }
    }
}

// 抗锯齿实心圆: 主体普通填充, 边缘 r..r+1 环覆盖度混合
void gfx_disc_aa(int cx, int cy, int rr, uint8_t r, uint8_t g, uint8_t b) {
    if (rr < 0) return;
    if (!g_lfb) {
        uint8_t idx = rgb_to_idx(r, g, b);
        int r2 = rr * rr;
        for (int y = -rr; y <= rr; y++)
            for (int x = -rr; x <= rr; x++)
                if (x * x + y * y <= r2) gfx_pixel_idx(cx + x, cy + y, idx);
        return;
    }
    int r2 = rr * rr;
    int r2o = (rr + 1) * (rr + 1);
    for (int y = -rr - 1; y <= rr + 1; y++) {
        for (int x = -rr - 1; x <= rr + 1; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2) gfx_pixel_rgb(cx + x, cy + y, r, g, b);
            else if (d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(cx + x, cy + y, r, g, b, cov);
            }
        }
    }
}

// 抗锯齿圆角矩形 (填充): 主体 gfx_fill_rgb, 四角按距离场插值
void gfx_fill_round_rgb_aa(int x0, int y0, int x1, int y1, int rad,
                           uint8_t r, uint8_t g, uint8_t b) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    rad = round_r_clamp(w, h, rad);
    if (rad <= 0) { gfx_fill_rgb(x0, y0, x1, y1, r, g, b); return; }
    if (!g_lfb) {
        gfx_fill_round_idx(x0, y0, x1, y1, rad, rgb_to_idx(r, g, b));
        return;
    }
    gfx_fill_rgb(x0 + rad, y0, x1 - rad, y1, r, g, b);
    gfx_fill_rgb(x0, y0 + rad, x1, y1 - rad, r, g, b);
    int cr = rad;
    int r2 = cr * cr;
    int r2o = (cr + 1) * (cr + 1);
    for (int y = 0; y < cr; y++)
        for (int x = 0; x < cr; x++) {
            int dx = cr - x, dy = cr - y;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) gfx_pixel_rgb(x0 + x, y0 + y, r, g, b);
            else if (d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x0 + x, y0 + y, r, g, b, cov);
            }
        }
    for (int y = 0; y < cr; y++)
        for (int x = 0; x < cr; x++) {
            int dx = x, dy = cr - y;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) gfx_pixel_rgb(x1 - x, y0 + y, r, g, b);
            else if (d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x1 - x, y0 + y, r, g, b, cov);
            }
        }
    for (int y = 0; y < cr; y++)
        for (int x = 0; x < cr; x++) {
            int dx = cr - x, dy = y;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) gfx_pixel_rgb(x0 + x, y1 - y, r, g, b);
            else if (d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x0 + x, y1 - y, r, g, b, cov);
            }
        }
    for (int y = 0; y < cr; y++)
        for (int x = 0; x < cr; x++) {
            int dx = x, dy = y;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) gfx_pixel_rgb(x1 - x, y1 - y, r, g, b);
            else if (d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x1 - x, y1 - y, r, g, b, cov);
            }
        }
}

// 抗锯齿圆角矩形 (描边)
void gfx_round_rect_rgb_aa(int x0, int y0, int x1, int y1, int rad,
                           uint8_t r, uint8_t g, uint8_t b) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    rad = round_r_clamp(w, h, rad);
    if (rad <= 0) { gfx_rect_rgb(x0, y0, x1, y1, r, g, b); return; }
    if (!g_lfb) {
        gfx_round_rect_idx(x0, y0, x1, y1, rad, rgb_to_idx(r, g, b));
        return;
    }
    gfx_fill_rgb(x0 + rad, y0, x1 - rad, y0, r, g, b);
    gfx_fill_rgb(x0 + rad, y1, x1 - rad, y1, r, g, b);
    gfx_fill_rgb(x0, y0 + rad, x0, y1 - rad, r, g, b);
    gfx_fill_rgb(x1, y0 + rad, x1, y1 - rad, r, g, b);
    int cr = rad;
    int r2 = cr * cr;
    int r2i = (cr - 1) * (cr - 1);
    int r2o = (cr + 1) * (cr + 1);
    for (int y = 0; y <= cr; y++)
        for (int x = 0; x <= cr; x++) {
            int dx = cr - x, dy = cr - y;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2i && d2 <= r2) gfx_pixel_rgb(x0 + x, y0 + y, r, g, b);
            else if (d2 > r2 && d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x0 + x, y0 + y, r, g, b, cov);
            }
        }
    for (int y = 0; y <= cr; y++)
        for (int x = 0; x <= cr; x++) {
            int dx = x, dy = cr - y;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2i && d2 <= r2) gfx_pixel_rgb(x1 - x, y0 + y, r, g, b);
            else if (d2 > r2 && d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x1 - x, y0 + y, r, g, b, cov);
            }
        }
    for (int y = 0; y <= cr; y++)
        for (int x = 0; x <= cr; x++) {
            int dx = cr - x, dy = y;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2i && d2 <= r2) gfx_pixel_rgb(x0 + x, y1 - y, r, g, b);
            else if (d2 > r2 && d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x0 + x, y1 - y, r, g, b, cov);
            }
        }
    for (int y = 0; y <= cr; y++)
        for (int x = 0; x <= cr; x++) {
            int dx = x, dy = y;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2i && d2 <= r2) gfx_pixel_rgb(x1 - x, y1 - y, r, g, b);
            else if (d2 > r2 && d2 <= r2o) {
                int cov = (r2o - d2) * 256 / (r2o - r2);
                blend_px_logical(x1 - x, y1 - y, r, g, b, cov);
            }
        }
}

// 真彩垂直渐变: 逐行 8bit 线性插值
void gfx_gradient_v_rgb(int x0, int y0, int x1, int y1,
                        uint8_t r1, uint8_t g1, uint8_t b1,
                        uint8_t r2, uint8_t g2, uint8_t b2) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int h = y1 - y0;
    if (h <= 0) { gfx_pixel_rgb(x0, y0, r1, g1, b1); return; }
    for (int y = y0; y <= y1; y++) {
        int t = (y - y0) * 256 / h;
        uint8_t r = (uint8_t)(r1 + (((int)r2 - r1) * t >> 8));
        uint8_t g = (uint8_t)(g1 + (((int)g2 - g1) * t >> 8));
        uint8_t b = (uint8_t)(b1 + (((int)b2 - b1) * t >> 8));
        gfx_fill_rgb(x0, y, x1, y, r, g, b);
    }
}

// 8bpp 色带分层渐变: 已登记色索引分层 + 隔行双档交替
void gfx_gradient_v_idx(int x0, int y0, int x1, int y1, uint8_t s, uint8_t e) {
    if (s > e) { uint8_t t = s; s = e; e = t; }
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    if (g_lfb) {
        uint8_t r1, g1, b1, r2, g2, b2;
        idx_to_rgb(s, &r1, &g1, &b1);
        idx_to_rgb(e, &r2, &g2, &b2);
        gfx_gradient_v_rgb(x0, y0, x1, y1, r1, g1, b1, r2, g2, b2);
        return;
    }
    int h = y1 - y0 + 1;
    int bands = (int)e - (int)s + 1;
    if (bands <= 1) { gfx_fill_idx(x0, y0, x1, y1, s); return; }
    for (int y = y0; y <= y1; y++) {
        int band = (y - y0) * bands / h;
        if (band >= bands) band = bands - 1;
        uint8_t idx = s + (uint8_t)band;
        if ((y - y0) & 1) {
            int next = band + 1;
            if (next < bands) idx = s + (uint8_t)next;
        }
        gfx_fill_idx(x0, y, x1, y, idx);
    }
}

// 8bpp 图标过渡色助手: 返回 fg 相对 bg 的 1 级过渡索引
uint8_t gfx_trans_idx(uint8_t fg, uint8_t bg) {
    static const struct { uint8_t fg, bg, trans; } table[] = {
        {COL_WHITE,  COL_PANEL,    COL_PANEL_HI},
        {COL_LGREEN, COL_PANEL,    COL_FIELD},
        {COL_LRED,   COL_PANEL,    COL_LRED},
        {COL_YELLOW, COL_PANEL,    COL_ORANGE},
        {COL_WHITE,  COL_BLUE,     COL_LBLUE},
        {COL_WHITE,  COL_TITLEBG,  COL_PANEL},
        {COL_WHITE,  COL_TASKBAR,  COL_TASK_HI},
        {COL_LBLUE,  COL_PANEL,    COL_PANEL_HI},
    };
    for (int i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++) {
        if (table[i].fg == fg && table[i].bg == bg) return table[i].trans;
    }
    return bg;
}

void gfx_flip(void) {
    // 后台 -> 前台, 行级脏拷贝: 逐行比较, 只有相对前台变化了的行才拷贝.
    // (静止桌面与前台完全一致 -> 全程零写屏, 消除闪烁与撕裂;
    //  鼠标指针移动/窗口动画只改写其影响的行 -> 写放大从整屏降到几 KB,
    //  VM/TCG 下每帧开销大减, wm 循环更快, 指针移动更稳更跟手.)
    if (!g_back || !g_front || g_buf_w <= 0 || g_buf_h <= 0) return;
    const size_t rb = (size_t)g_buf_pitch;   // 每行字节数 (实际缓冲跨度)
    uint8_t* f = (uint8_t*)g_front;
    const uint8_t* b = (const uint8_t*)g_back;
    for (int y = 0; y < g_buf_h; y++) {
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

// 注: gop_info_t / GOP_INFO_ADDR / GOP_INFO_MAGIC 已在文件顶部声明 (供 gfx_init 使用).

static int      g_gop_state  = 0;       // 0=未探测 1=有效 -1=不可用(BIOS 路径)
static uint32_t g_gop_w, g_gop_h, g_gop_pitch, g_gop_fmt;
static uint8_t* g_gop_fb;
static uint32_t g_gop_lut[256];
static uint32_t g_gop_last_ms;
// ---- 防闪烁呈现状态 ----
static uint32_t g_gop_disp_w, g_gop_disp_h;   // 等比缩放后实际铺设的像素尺寸
static int32_t  g_gop_offx_i, g_gop_offy_i;   // 居中黑边偏移(目标像素)
static uint32_t g_gop_stepx,  g_gop_stepy;     // 16.16 定点: 每目标像素对应的源像素数
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

// 定点(16.16)双线性插值, 用于平滑放大, 避免内核浮点(中断上下文安全)
static inline uint32_t lerp_i(uint32_t a, uint32_t b, uint32_t t) { // t: 0..65535
    uint32_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    uint32_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    uint32_t r  = ar + (uint32_t)(((int)(br - ar) * (int)t + 32768) >> 16);
    uint32_t g  = ag + (uint32_t)(((int)(bg - ag) * (int)t + 32768) >> 16);
    uint32_t bl = ab + (uint32_t)(((int)(bb - ab) * (int)t + 32768) >> 16);
    return (r << 16) | (g << 8) | bl;
}
static inline uint32_t bilinear_i(uint32_t c00, uint32_t c10,
                                  uint32_t c01, uint32_t c11,
                                  uint32_t tx, uint32_t ty) {
    return lerp_i(lerp_i(c00, c10, tx), lerp_i(c01, c11, tx), ty);
}

void gfx_gop_present(void) {
    if (g_native) return;   // UEFI 原生路径: gfx_flip 已直写 GOP 帧缓冲, 无需镜像放大
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
        // 等比定点缩放几何(16.16), 只算一次: 把 320x200 平滑铺满, 居中留黑边
        uint64_t fscl;
        if ((uint64_t)g_gop_w * 200 <= (uint64_t)g_gop_h * 320)
            fscl = ((uint64_t)g_gop_w << 16) / 320;        // 宽度受限
        else
            fscl = ((uint64_t)g_gop_h << 16) / 200;        // 高度受限
        g_gop_disp_w = (uint32_t)((320ULL * fscl) >> 16);
        g_gop_disp_h = (uint32_t)((200ULL * fscl) >> 16);
        g_gop_offx_i = (int32_t)(g_gop_w - g_gop_disp_w) / 2;
        g_gop_offy_i = (int32_t)(g_gop_h - g_gop_disp_h) / 2;
        g_gop_stepx  = (uint32_t)((320ULL << 16) / g_gop_disp_w);  // 源/目标 像素比
        g_gop_stepy  = (uint32_t)((200ULL << 16) / g_gop_disp_h);
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

    // 首次: 整屏铺背景色消除黑边 (只铺一次, 之后不动 -> 不再反复清屏闪烁)
    if (!g_gop_bg_done) {
        g_gop_bg_done = 1;
        uint32_t bg = gop_rgb8(gfx_palette[26]);   // WALL_F (26): 与新壁纸渐变的深色底一致
        // GOP PixelFormat: 0=RGBX(低字节=红), 1=BGRX(低字节=蓝).
        if (g_gop_fmt == 0) bg = ((bg & 0xFFu) << 16) | (bg & 0xFF00u) | (bg >> 16);
        for (uint32_t by = 0; by < g_gop_h; by++) {
            volatile uint32_t* brow = (volatile uint32_t*)(g_gop_fb + (size_t)by * g_gop_pitch);
            for (uint32_t bx = 0; bx < g_gop_w; bx++) brow[bx] = bg;
        }
    }

    // 行级脏检测 + 双线性定点平滑放大: 仅把变化源行对应的目标行带用相邻源行 2D
    // 插值重绘 -> 彻底消除整数放大产生的大方块像素; 静止区域零开销.
    for (uint32_t y = 0; y < 200; y++) {
        const uint8_t* cur = src + (size_t)y * 320;
        uint8_t* sn = g_gop_snap + (size_t)y * 320;
        int same = 1;
        for (uint32_t x = 0; x < 320; x++)
            if (sn[x] != cur[x]) { same = 0; break; }
        if (same) continue;
        for (uint32_t x = 0; x < 320; x++) sn[x] = cur[x];   // 快照同步

        int dy0 = g_gop_offy_i + (int)((uint64_t)y      * g_gop_stepy >> 16);
        int dy1 = g_gop_offy_i + (int)((uint64_t)(y + 1) * g_gop_stepy >> 16);
        if (dy1 <= dy0) dy1 = dy0 + 1;
        if (dy0 < 0) dy0 = 0;
        if (dy1 > (int)g_gop_h) dy1 = (int)g_gop_h;
        for (int dy = dy0; dy < dy1; dy++) {
            uint64_t sy16 = (uint64_t)(dy - g_gop_offy_i) * g_gop_stepy;
            int y0 = (int)(sy16 >> 16); if (y0 < 0) y0 = 0; if (y0 > 199) y0 = 199;
            int y1 = y0 + 1;             if (y1 > 199) y1 = 199;
            uint32_t ty = (uint32_t)(sy16 & 0xFFFF);
            const uint8_t* r0 = src + (size_t)y0 * 320;
            const uint8_t* r1 = src + (size_t)y1 * 320;
            volatile uint32_t* drow = (volatile uint32_t*)(g_gop_fb +
                (size_t)dy * g_gop_pitch + (size_t)g_gop_offx_i * 4);
            for (uint32_t dx = 0; dx < g_gop_disp_w; dx++) {
                uint64_t sx16 = (uint64_t)dx * g_gop_stepx;
                int x0 = (int)(sx16 >> 16); if (x0 < 0) x0 = 0; if (x0 > 319) x0 = 319;
                int x1 = x0 + 1;          if (x1 > 319) x1 = 319;
                uint32_t tx = (uint32_t)(sx16 & 0xFFFF);
                uint32_t c00 = g_gop_lut[r0[x0]], c10 = g_gop_lut[r0[x1]];
                uint32_t c01 = g_gop_lut[r1[x0]], c11 = g_gop_lut[r1[x1]];
                uint32_t c = bilinear_i(c00, c10, c01, c11, tx, ty);
                if (g_gop_fmt == 0) c = ((c & 0xFFu) << 16) | (c & 0xFF00u) | (c >> 16);
                drow[dx] = c;
            }
        }
    }
}
