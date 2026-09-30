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
    0x090D16, // 21 WALL_A：蓝紫极光的亮部（6-bit DAC）
    0x070B14, // 22 WALL_B
    0x060911, // 23 WALL_C
    0x05070E, // 24 WALL_D
    0x03060B, // 25 WALL_E
    0x020408, // 26 WALL_F：夜色底部
    0x1A2A3F, // 27 ACCENT：清晰、低饱和的系统蓝
    0x101B2B, // 28 ACCENT_SOFT：玻璃上的悬停蓝
    0x050607, // 29 TASKBAR：深色 Dock / 菜单底
    0x0B0C0F, // 30 TASK_HI：Dock / 菜单悬停
    0x020204, // 31 SHADOW：阴影/描边
};

// 现代 UI 柔和扩展色 160..165 (与 vga.h COL_UI_* 同步; 每字节为 6-bit DAC 值 0..63,
// 与 gfx_palette 存法一致, 经 idx_to_rgb 高 6 位展开为 8bit)
// 均为低饱和柔和色: 状态色可区分、大面积背景低饱和、标题/Dock 悬停层次分明。
const uint32_t gfx_palette_ext[GFX_COLORS_EXT] = {
    /* 160..180: full 8-bit RGB theme tokens for native GOP. */
    0x0B1220, /* BG */
    0x121C2F, /* PANEL */
    0x0F182A, /* TITLE */
    0x0A1322, /* TASKBAR */
    0x101B2E, /* MENU */
    0xF5F7FA, /* FG */
    0xB4BFCD, /* FG_SOFT */
    0xFFFFFF, /* FG_TITLE */
    0x4AA8FF, /* ACCENT */
    0x235D91, /* ACCENT_SOFT */
    0x2A3A50, /* BORDER */
    0x5EB6FF, /* BORDER_FOCUS */
    0x02060D, /* SHADOW */
    0x1B2A40, /* HOVER */
    0x284765, /* PRESSED */
    0x273244, /* DISABLED */
    0x34D399, /* SUCCESS */
    0xF59E0B, /* WARNING */
    0xF87171, /* DANGER */
    0x0D1727, /* FIELD */
    0x17253A, /* FIELD_FOCUS */
    0x8AB4F8, /* legacy soft accent */
    0x334155, /* neutral */
    0x1E293B  /* neutral 2 */
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

// ---- 脏区局部重绘: 绘制裁剪框 ----
// 逻辑坐标裁剪框(闭区间); 同时推导原生(后台缓冲)坐标裁剪框。
// 桌面 LFB 路径 scale=1/off=0; 其它路径 scale/off 也可能 != 1, 两框均维护以确保正确。
static int g_clip_on = 0;
static int g_clip_x0 = 0, g_clip_y0 = 0, g_clip_x1 = -1, g_clip_y1 = -1;    // 逻辑
static int g_nclip_x0 = 0, g_nclip_y0 = 0, g_nclip_x1 = -1, g_nclip_y1 = -1; // 原生

void gfx_set_clip(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_w - 1) x1 = g_w - 1;
    if (y1 > g_h - 1) y1 = g_h - 1;
    g_clip_on = 1;
    if (x1 < x0 || y1 < y0) { g_clip_x1 = -1; g_nclip_x1 = -1; return; }  // 空区域 -> 全跳过
    g_clip_x0 = x0; g_clip_y0 = y0; g_clip_x1 = x1; g_clip_y1 = y1;
    int sc = g_scale;
    g_nclip_x0 = x0 * sc + g_off_x;
    g_nclip_y0 = y0 * sc + g_off_y;
    g_nclip_x1 = (x1 + 1) * sc - 1 + g_off_x;
    g_nclip_y1 = (y1 + 1) * sc - 1 + g_off_y;
}
void gfx_reset_clip(void) { g_clip_on = 0; }

// 查询当前裁剪框是否与给定逻辑矩形相交 (无裁剪时恒真)。
// 供 wm.c 在局部重绘时跳过完全位于脏区域之外的整幅绘制, 大幅降低无效迭代。
int gfx_clip_active(void) { return g_clip_on; }
int gfx_clip_intersects(int x0, int y0, int x1, int y1) {
    if (!g_clip_on) return 1;
    if (g_clip_x1 < g_clip_x0) return 0;          // 空裁剪框
    if (x1 < g_clip_x0 || x0 > g_clip_x1 || y1 < g_clip_y0 || y0 > g_clip_y1) return 0;
    return 1;
}

// 字形/几何: 原生坐标包围盒是否与当前裁剪框相交 (供局部重绘时整字/整形跳过)
static inline int g_clip_box_hit_native(int bx0, int by0, int bx1, int by1) {
    if (g_nclip_x1 < g_nclip_x0) return 0;
    if (bx1 < g_nclip_x0 || bx0 > g_nclip_x1 || by1 < g_nclip_y0 || by0 > g_nclip_y1) return 0;
    return 1;
}

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
    if (g_clip_on) {   // 脏区裁剪 (原生坐标)
        if (g_nclip_x1 < g_nclip_x0) return;
        if (rx < g_nclip_x0 || rx > g_nclip_x1 || ry < g_nclip_y0 || ry > g_nclip_y1) return;
    }
    uint8_t* p = g_back + (size_t)ry * g_buf_pitch + (size_t)rx * g_bpp_bytes;
    if (g_bpp_bytes == 4)      *(uint32_t*)p = px;
    else if (g_bpp_bytes == 2) *(uint16_t*)p = (uint16_t)px;
    else                       *p = (uint8_t)px;
}

// 写逻辑像素: 自动按 g_scale 放大成 g_scale x g_scale 的原生像素块.
// 所有图元都经此出口 -> 形状在原生分辨率下锐利; mode13h 时 g_scale=1 行为不变.
static inline void put_px(int x, int y, uint32_t px) {
    if (g_clip_on) {   // 脏区裁剪 (逻辑坐标)
        if (g_clip_x1 < g_clip_x0) return;
        if (x < g_clip_x0 || x > g_clip_x1 || y < g_clip_y0 || y > g_clip_y1) return;
    }
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
    g_native = 1; g_scale = 1; g_off_x = 0; g_off_y = 0;
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
    // 直接以原生 32bpp 初始化 LFB。逻辑画布按 GOP 尺寸/整数缩放动态扩展，
    // 让 320x200 时代的应用仍可运行，同时消除旧版居中黑/蓝边。
    {
        const gop_info_t* gi = (const gop_info_t*)(uintptr_t)GOP_INFO_ADDR;
        if (gi->magic == GOP_INFO_MAGIC && gi->fb_addr != 0 &&
            gi->width >= 320 && gi->height >= 200 && gi->pitch >= gi->width * 4) {
            /* First-principles GUI pipeline: the GOP framebuffer is the actual canvas.
               Legacy 320x200 assumptions are removed here; the shell and layout code
               receive physical pixels so a 1920x1080 desktop is really 1920x1080. */
            int logical_w = (int)gi->width;
            int logical_h = (int)gi->height;
            int ui_font_scale = (logical_w >= 1600 && logical_h >= 900) ? 2 :
                                ((logical_w >= 1000 && logical_h >= 600) ? 1 : 1);
            gfx_ser_str("[gfx] activating GOP native path ");
            gfx_ser_hex((uint32_t)logical_w); gfx_ser_putc('x'); gfx_ser_hex((uint32_t)logical_h);
            gfx_ser_str(" font_scale="); gfx_ser_hex((uint32_t)ui_font_scale); gfx_ser_str("\r\n");
            g_native = 1;
            g_lfb = 1;
            g_w = logical_w; g_h = logical_h;
            g_buf_w = (int)gi->width;
            g_buf_h = (int)gi->height;
            g_buf_pitch = (int)gi->pitch;
            g_bpp = 32; g_bpp_bytes = 4; g_rsize = 8;
            // 与旧 gfx_gop_present 一致: fmt==0(GOP RGBX) 需 R/B 交换 -> 存 0x00BBGGRR (LE byte0=R)
            if (gi->format == 0) { g_rshift = 0;  g_gshift = 8; g_bshift = 16; }  // RGBX
            else                 { g_rshift = 16; g_gshift = 8; g_bshift = 0;  }  // BGRX
            g_front = (void*)(uintptr_t)gi->fb_addr;
            g_scale = 1;
            g_font_scale = 1;
            g_off_x = 0;
            g_off_y = 0;
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
    if (g_clip_on && g_clip_x1 >= g_clip_x0) {   // 脏区局部重绘: 逻辑矩形与裁剪框求交
        if (x0 < g_clip_x0) x0 = g_clip_x0;
        if (y0 < g_clip_y0) y0 = g_clip_y0;
        if (x1 > g_clip_x1) x1 = g_clip_x1;
        if (y1 > g_clip_y1) y1 = g_clip_y1;
        if (x1 < x0 || y1 < y0) return;
    }
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
    if (g_clip_on && !gfx_clip_intersects(x, y, x + 7, y + 7)) return;   // 整字跳过
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
    if (g_clip_on && !g_clip_box_hit_native(ox + g_off_x, oy + g_off_y, ox + g_off_x + 8*sc - 1, oy + g_off_y + 8*sc - 1)) return;
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

// ---- 透明背景字形: 与 AA 字形同一套双线性覆盖计算, 但不加底色,
//      直接把字形覆盖度混合到现有帧 (供 macOS 玻璃顶栏 / 壁纸表面绘制文字) ----
static inline void alpha_blend_native(int rx, int ry, uint8_t r, uint8_t g, uint8_t b, int cov);

// 透明背景 8x8 ASCII 字形
void gfx_draw_char_over(int x, int y, char ch, uint8_t fr, uint8_t fg, uint8_t fb) {
    const uint8_t* font = (const uint8_t*)g_font8x8;
    uint8_t c = (uint8_t)ch;
    if (c >= 128) c = '?';
    if (c < 0x20) c = 0x20;
    const uint8_t* gl = &font[(c - 0x20) * 8];
    int sc = g_scale;
    int ox = x * sc, oy = y * sc;
    if (g_clip_on && !g_clip_box_hit_native(ox + g_off_x, oy + g_off_y, ox + g_off_x + 8*sc - 1, oy + g_off_y + 8*sc - 1)) return;
    for (int ry = 0; ry < 8 * sc; ry++) {
        int pys = (ry << 16) / sc;
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
            int cov = top + (((bot - top) * ty) >> 16);
            if (cov <= 0) continue;
            alpha_blend_native(ox + rx + g_off_x, oy + ry + g_off_y, fr, fg, fb, cov >> 8);
        }
    }
}

// 透明背景 16x16 CJK 字形
void gfx_draw_cjk_aa_over(int x, int y, const uint16_t* rows, uint8_t fr, uint8_t fg, uint8_t fb) {
    if (!rows) return;
    int sc = g_scale;
    int ox = x * sc, oy = y * sc;
    if (g_clip_on && !g_clip_box_hit_native(ox + g_off_x, oy + g_off_y, ox + g_off_x + 16*sc - 1, oy + g_off_y + 16*sc - 1)) return;
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
            if (cov <= 0) continue;
            alpha_blend_native(ox + rx + g_off_x, oy + ry + g_off_y, fr, fg, fb, cov >> 8);
        }
    }
}

// 透明背景 24x24 CJK 字形
void gfx_draw_cjk24_aa_over(int x, int y, const uint32_t* rows, uint8_t fr, uint8_t fg, uint8_t fb) {
    if (!rows) return;
    int sc = g_scale;
    int ox = x * sc, oy = y * sc;
    if (g_clip_on && !g_clip_box_hit_native(ox + g_off_x, oy + g_off_y, ox + g_off_x + 24*sc - 1, oy + g_off_y + 24*sc - 1)) return;
    for (int ry = 0; ry < 24 * sc; ry++) {
        int sy = ry / sc;
        for (int rx = 0; rx < 24 * sc; rx++) {
            int sx = rx / sc;
            int on = (rows[sy] >> (23 - sx)) & 1u;
            if (!on) continue;
            alpha_blend_native(ox + rx + g_off_x, oy + ry + g_off_y, fr, fg, fb, 256);
        }
    }
}

// 导出: CJK 16x16 位图抗锯齿放大 (供 cjk.c 在原生模式下调用)
void gfx_draw_cjk_aa(int x, int y, const uint16_t* rows,
                     uint8_t fr, uint8_t fg, uint8_t fb,
                     uint8_t br, uint8_t bg, uint8_t bb) {
    int sc = g_scale;
    int ox = x * sc, oy = y * sc;   // put_px_real 内部再加 g_off_x/y 居中偏移
    if (g_clip_on && !g_clip_box_hit_native(ox + g_off_x, oy + g_off_y, ox + g_off_x + 16*sc - 1, oy + g_off_y + 16*sc - 1)) return;
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

void gfx_draw_cjk24_aa(int x, int y, const uint32_t* rows,
                       uint8_t fr, uint8_t fg, uint8_t fb,
                       uint8_t br, uint8_t bg, uint8_t bb) {
    if (!rows) return;
    int sc = g_scale;
    int ox = x * sc, oy = y * sc;
    if (g_clip_on && !g_clip_box_hit_native(ox + g_off_x, oy + g_off_y, ox + g_off_x + 24*sc - 1, oy + g_off_y + 24*sc - 1)) return;
    /* Render the complete 24x24 source bitmap.  Do not downsample: that was the
       direct cause of broken Chinese strokes in the previous UI renderer. */
    for (int ry = 0; ry < 24 * sc; ry++) {
        int sy = ry / sc;
        for (int rx = 0; rx < 24 * sc; rx++) {
            int sx = rx / sc;
            int on = (rows[sy] >> (23 - sx)) & 1u;
            uint8_t R = on ? fr : br;
            uint8_t G = on ? fg : bg;
            uint8_t B = on ? fb : bb;
            put_px_real(ox + rx, oy + ry, pack_rgb(R, G, B));
        }
    }
}

// 导出: 调色板索引 -> 8bit RGB (供 vga.c 转换 COL_* 字体色)
void gfx_idx_rgb(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint32_t c = palette_color(idx);
    if (idx >= COL_UI_BASE && idx < COL_UI_BASE + GFX_COLORS_EXT) {
        *r = (uint8_t)((c >> 16) & 0xFF);
        *g = (uint8_t)((c >> 8) & 0xFF);
        *b = (uint8_t)(c & 0xFF);
        return;
    }
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
    if (idx >= COL_UI_BASE && idx < COL_UI_BASE + GFX_COLORS_EXT) {
        *r = (uint8_t)((c >> 16) & 0xFF);
        *g = (uint8_t)((c >> 8) & 0xFF);
        *b = (uint8_t)(c & 0xFF);
        return;
    }
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
    if (g_clip_on && g_clip_x1 >= g_clip_x0) {   // 脏区局部重绘 (mode13h 路径)
        if (x0 < g_clip_x0) x0 = g_clip_x0;
        if (y0 < g_clip_y0) y0 = g_clip_y0;
        if (x1 > g_clip_x1) x1 = g_clip_x1;
        if (y1 > g_clip_y1) y1 = g_clip_y1;
        if (x1 < x0 || y1 < y0) return;
    }
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
    if (g_clip_on) {   // 脏区裁剪 (原生坐标)
        if (g_nclip_x1 < g_nclip_x0) return;
        if (rx < g_nclip_x0 || rx > g_nclip_x1 || ry < g_nclip_y0 || ry > g_nclip_y1) return;
    }
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

// 半透明矩形填充 (逻辑坐标, a=0..256): macOS 风格顶栏/悬浮面板的玻璃质感基础
void gfx_fill_rgb_alpha(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, int a) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    if (!g_lfb) { if (a >= 128) gfx_fill_idx(x0, y0, x1, y1, rgb_to_idx(r, g, b)); return; }
    if (a <= 0) return;
    if (a > 256) a = 256;
    if (g_clip_on && g_clip_x1 >= g_clip_x0) {   // 脏区局部重绘: 逻辑矩形与裁剪框求交
        if (x0 < g_clip_x0) x0 = g_clip_x0;
        if (y0 < g_clip_y0) y0 = g_clip_y0;
        if (x1 > g_clip_x1) x1 = g_clip_x1;
        if (y1 > g_clip_y1) y1 = g_clip_y1;
        if (x1 < x0 || y1 < y0) return;
    }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            blend_px_logical(x, y, r, g, b, a);
}

// 半透明圆角矩形填充 (逻辑坐标): 内部整块 alpha, 圆角外不混合
void gfx_fill_round_rgb_alpha(int x0, int y0, int x1, int y1, int rad,
                              uint8_t r, uint8_t g, uint8_t b, int a) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    if (!g_lfb) { if (a >= 128) gfx_fill_round_idx(x0, y0, x1, y1, rad, rgb_to_idx(r, g, b)); return; }
    if (a <= 0) return;
    if (a > 256) a = 256;
    if (g_clip_on && g_clip_x1 >= g_clip_x0) {   // 脏区局部重绘: 逻辑矩形与裁剪框求交
        if (x0 < g_clip_x0) x0 = g_clip_x0;
        if (y0 < g_clip_y0) y0 = g_clip_y0;
        if (x1 > g_clip_x1) x1 = g_clip_x1;
        if (y1 > g_clip_y1) y1 = g_clip_y1;
        if (x1 < x0 || y1 < y0) return;
    }
    if (rad <= 0) { gfx_fill_rgb_alpha(x0, y0, x1, y1, r, g, b, a); return; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    int m = w < h ? w : h;
    if (rad > m / 2) rad = m / 2;
    int r2 = rad * rad;
    int cxl = x0 + rad, cxr = x1 - rad, cyt = y0 + rad, cyb = y1 - rad;
    for (int y = y0; y <= y1; y++) {
        int inTop = y < cyt, inBot = y > cyb;
        for (int x = x0; x <= x1; x++) {
            int inside = 1;
            if (inTop && x < cxl) { int dx = cxl - x, dy = cyt - y; if (dx * dx + dy * dy > r2) inside = 0; }
            else if (inTop && x > cxr) { int dx = x - cxr, dy = cyt - y; if (dx * dx + dy * dy > r2) inside = 0; }
            else if (inBot && x < cxl) { int dx = cxl - x, dy = y - cyb; if (dx * dx + dy * dy > r2) inside = 0; }
            else if (inBot && x > cxr) { int dx = x - cxr, dy = y - cyb; if (dx * dx + dy * dy > r2) inside = 0; }
            if (inside) blend_px_logical(x, y, r, g, b, a);
        }
    }
}

// 毛玻璃模糊 (盒式模糊): LFB 32bpp 路径, 对后台缓冲指定区域做 radius 窗口均值模糊。
// 8bpp 路径下为空实现 (调用方走降级分支)。使用可分离两遍盒式模糊 (水平+垂直)。
void gfx_blur_rgb(int x0, int y0, int x1, int y1, int radius) {
    if (!g_lfb || g_bpp_bytes != 4) return;  // 仅 LFB 32bpp
    if (radius <= 0) return;
    if (radius > 8) radius = 8;
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= g_w) x1 = g_w - 1;
    if (y1 >= g_h) y1 = g_h - 1;
    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;
    if (w <= 0 || h <= 0) return;

    // 临时缓冲: 水平模糊后的中间结果 (每像素 3 字节 R,G,B)
    size_t tmp_sz = (size_t)w * (size_t)h * 3;
    uint8_t* tmp = (uint8_t*)kmalloc(tmp_sz);
    if (!tmp) return;  // 内存不足, 跳过模糊

    // 水平遍: 对每行做 radius 窗口滑动均值
    for (int y = 0; y < h; y++) {
        uint32_t* row = (uint32_t*)(g_back + (size_t)(y + y0) * g_buf_pitch);
        int sumR = 0, sumG = 0, sumB = 0, cnt = 0;
        // 初始化窗口 [0, radius]
        for (int k = 0; k <= radius && k < w; k++) {
            uint32_t px = row[x0 + k];
            sumR += (px >> g_rshift) & 0xFF;
            sumG += (px >> g_gshift) & 0xFF;
            sumB += (px >> g_bshift) & 0xFF;
            cnt++;
        }
        for (int x = 0; x < w; x++) {
            uint8_t* t = tmp + ((size_t)y * w + x) * 3;
            t[0] = (uint8_t)(sumR / cnt);
            t[1] = (uint8_t)(sumG / cnt);
            t[2] = (uint8_t)(sumB / cnt);
            // 滑动窗口: 移出左端, 移入右端
            int xOut = x - radius;
            int xIn  = x + radius + 1;
            if (xOut >= 0) {
                uint32_t px = row[x0 + xOut];
                sumR -= (px >> g_rshift) & 0xFF;
                sumG -= (px >> g_gshift) & 0xFF;
                sumB -= (px >> g_bshift) & 0xFF;
                cnt--;
            }
            if (xIn < w) {
                uint32_t px = row[x0 + xIn];
                sumR += (px >> g_rshift) & 0xFF;
                sumG += (px >> g_gshift) & 0xFF;
                sumB += (px >> g_bshift) & 0xFF;
                cnt++;
            }
        }
    }

    // 垂直遍: 对每列做 radius 窗口滑动均值, 写回后台缓冲
    for (int x = 0; x < w; x++) {
        int sumR = 0, sumG = 0, sumB = 0, cnt = 0;
        for (int k = 0; k <= radius && k < h; k++) {
            uint8_t* t = tmp + ((size_t)k * w + x) * 3;
            sumR += t[0]; sumG += t[1]; sumB += t[2]; cnt++;
        }
        for (int y = 0; y < h; y++) {
            uint32_t* px = (uint32_t*)(g_back + (size_t)(y + y0) * g_buf_pitch);
            px[x0 + x] = pack_rgb((uint8_t)(sumR / cnt), (uint8_t)(sumG / cnt), (uint8_t)(sumB / cnt));
            int yOut = y - radius;
            int yIn  = y + radius + 1;
            if (yOut >= 0) {
                uint8_t* t = tmp + ((size_t)yOut * w + x) * 3;
                sumR -= t[0]; sumG -= t[1]; sumB -= t[2]; cnt--;
            }
            if (yIn < h) {
                uint8_t* t = tmp + ((size_t)yIn * w + x) * 3;
                sumR += t[0]; sumG += t[1]; sumB += t[2]; cnt++;
            }
        }
    }

    kfree(tmp);
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
        int y0 = -rr, y1 = rr, x0 = -rr, x1 = rr;
        if (g_clip_on && g_clip_x1 >= g_clip_x0) {   // 循环级裁剪 (mode13h)
            int cy0 = g_clip_y0 - cy, cy1 = g_clip_y1 - cy;
            int cx0 = g_clip_x0 - cx, cx1 = g_clip_x1 - cx;
            if (y0 < cy0) y0 = cy0; if (y1 > cy1) y1 = cy1;
            if (x0 < cx0) x0 = cx0; if (x1 > cx1) x1 = cx1;
            if (y0 > y1 || x0 > x1) return;
        }
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                if (x * x + y * y <= r2) gfx_pixel_idx(cx + x, cy + y, idx);
        return;
    }
    int r2 = rr * rr;
    int r2o = (rr + 1) * (rr + 1);
    int y0 = -rr - 1, y1 = rr + 1, x0 = -rr - 1, x1 = rr + 1;
    if (g_clip_on && g_clip_x1 >= g_clip_x0) {   // 循环级裁剪: 仅遍历与裁剪框相交区域
        int cy0 = g_clip_y0 - cy, cy1 = g_clip_y1 - cy;
        int cx0 = g_clip_x0 - cx, cx1 = g_clip_x1 - cx;
        if (y0 < cy0) y0 = cy0; if (y1 > cy1) y1 = cy1;
        if (x0 < cx0) x0 = cx0; if (x1 > cx1) x1 = cx1;
        if (y0 > y1 || x0 > x1) return;
    }
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
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


// ================= 桌面壁纸缓存 (性能) =================
// 缩放(cover) + 上下遮罩原本每帧逐像素重算 (含每像素一次 64 位除法), 在 TCG 下
// 一帧 30 万像素的 64 位除法可达上百毫秒 —— 这是"帧率低"的主因。
// 这里把结果烘焙进缓存, 仅在源/尺寸变化时重建; 之后每帧只做整幅内存拷贝。
static uint32_t*       g_wall_cache = 0;
static int             g_wall_cw = 0, g_wall_ch = 0;
static const uint16_t* g_wall_src = 0;
static int             g_wall_sw = 0, g_wall_sh = 0;

static void gfx_wall_cache_drop(void) {
    if (g_wall_cache) { kfree(g_wall_cache); g_wall_cache = 0; }
    g_wall_cw = g_wall_ch = 0; g_wall_src = 0; g_wall_sw = g_wall_sh = 0;
}

static int gfx_wall_cache_build(const uint16_t* src, int sw, int sh, int dw, int dh,
                                int top_h, int bot_h, int top_a, int bot_a,
                                uint8_t mr, uint8_t mg, uint8_t mb) {
    g_wall_cache = (uint32_t*)kmalloc((size_t)dw * (size_t)dh * 4u);
    if (!g_wall_cache) return 0;
    g_wall_cw = dw; g_wall_ch = dh; g_wall_src = src; g_wall_sw = sw; g_wall_sh = sh;

    // 等比裁剪 (cover) 到目标宽高比
    int crop_w = sw, crop_h = sh, ox = 0, oy = 0;
    if ((uint64_t)dw * (uint64_t)sh > (uint64_t)dh * (uint64_t)sw) {
        crop_h = (int)((uint64_t)sw * (uint64_t)dh / (uint64_t)dw);
        if (crop_h < 1) crop_h = 1;
        oy = (sh - crop_h) / 2;
    } else if ((uint64_t)dw * (uint64_t)sh < (uint64_t)dh * (uint64_t)sw) {
        crop_w = (int)((uint64_t)sh * (uint64_t)dw / (uint64_t)dh);
        if (crop_w < 1) crop_w = 1;
        ox = (sw - crop_w) / 2;
    }
    for (int y = 0; y < dh; y++) {
        int sy = oy + (int)((uint64_t)y * (uint64_t)crop_h / (uint64_t)dh);
        if (sy >= sh) sy = sh - 1;
        const uint16_t* srow = src + (size_t)sy * (size_t)sw;
        uint32_t* drow = g_wall_cache + (size_t)y * (size_t)dw;
        int a = 0;
        if (top_h > 0 && y <= top_h) a = top_a;
        else if (bot_h > 0 && y >= dh - bot_h) a = bot_a;
        for (int x = 0; x < dw; x++) {
            int sx = ox + (int)((uint64_t)x * (uint64_t)crop_w / (uint64_t)dw);
            if (sx >= sw) sx = sw - 1;
            uint16_t p = srow[sx];
            uint8_t r = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
            uint8_t g = (uint8_t)(((p >> 5)  & 0x3F) * 255 / 63);
            uint8_t b = (uint8_t)((p & 0x1F) * 255 / 31);
            uint32_t px = pack_rgb(r, g, b);
            if (a > 0) {   // 烘焙上下遮罩 (与 gfx_fill_rgb_alpha 同公式)
                uint8_t cr = (uint8_t)((px >> g_rshift) & 0xFF);
                uint8_t cg = (uint8_t)((px >> g_gshift) & 0xFF);
                uint8_t cb = (uint8_t)((px >> g_bshift) & 0xFF);
                uint8_t R = (uint8_t)(cr + (((int)mr - cr) * a >> 8));
                uint8_t G = (uint8_t)(cg + (((int)mg - cg) * a >> 8));
                uint8_t B = (uint8_t)(cb + (((int)mb - cb) * a >> 8));
                px = pack_rgb(R, G, B);
            }
            drow[x] = px;
        }
    }
    return 1;
}

static int gfx_wall_cache_valid(const uint16_t* src, int sw, int sh, int dw, int dh) {
    return g_wall_cache && g_wall_src == src && g_wall_sw == sw && g_wall_sh == sh &&
           g_wall_cw == dw && g_wall_ch == dh;
}

// 呈现桌面壁纸 (LFB 32bpp): 缓存失效时重建, 随后整幅拷入后台缓冲。
// 返回 1 = 已处理; 0 = 非 32bpp LFB 路径 (调用方走 8bpp 程序化回退)。
int gfx_desktop_wallpaper_present(const uint16_t* src, int sw, int sh,
                                  int top_h, int bot_h, int top_a, int bot_a,
                                  uint8_t mr, uint8_t mg, uint8_t mb) {
    if (!g_back || !g_lfb || g_bpp_bytes != 4) return 0;
    int dw = g_buf_w, dh = g_buf_h;
    if (!gfx_wall_cache_valid(src, sw, sh, dw, dh)) {
        gfx_wall_cache_drop();
        if (!gfx_wall_cache_build(src, sw, sh, dw, dh, top_h, bot_h, top_a, bot_a, mr, mg, mb))
            return 1;   // 分配失败: 视作已处理(空), 避免每帧重试
    }
    // 脏区局部重绘: 仅拷贝裁剪框覆盖的行/列 (其余区域保留上一帧正确内容)
    int y0 = 0, y1 = dh - 1, x0 = 0, x1 = dw - 1;
    if (g_clip_on && g_nclip_x1 >= g_nclip_x0) {
        if (g_nclip_y0 > y0) y0 = g_nclip_y0;
        if (g_nclip_y1 < y1) y1 = g_nclip_y1;
        if (g_nclip_x0 > x0) x0 = g_nclip_x0;
        if (g_nclip_x1 < x1) x1 = g_nclip_x1;
        if (y1 < y0 || x1 < x0) return 1;
    }
    for (int y = y0; y <= y1; y++) {
        uint32_t* d = (uint32_t*)(g_back + (size_t)y * g_buf_pitch);
        const uint32_t* s = g_wall_cache + (size_t)y * (size_t)dw;
        for (int x = x0; x <= x1; x++) d[x] = s[x];
    }
    return 1;
}

void gfx_blit_rgb565_cover(const uint16_t* src, int sw, int sh, int dw, int dh) {
    if (!src || !g_back || !g_lfb || g_bpp_bytes != 4 || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (dw > g_buf_w) dw = g_buf_w;
    if (dh > g_buf_h) dh = g_buf_h;
    // Center-crop source to destination aspect ratio, then nearest-neighbor sample.
    int crop_w = sw, crop_h = sh, ox = 0, oy = 0;
    if ((uint64_t)dw * (uint64_t)sh > (uint64_t)dh * (uint64_t)sw) {
        crop_h = (int)((uint64_t)sw * (uint64_t)dh / (uint64_t)dw);
        if (crop_h < 1) crop_h = 1;
        oy = (sh - crop_h) / 2;
    } else if ((uint64_t)dw * (uint64_t)sh < (uint64_t)dh * (uint64_t)sw) {
        crop_w = (int)((uint64_t)sh * (uint64_t)dw / (uint64_t)dh);
        if (crop_w < 1) crop_w = 1;
        ox = (sw - crop_w) / 2;
    }
    for (int y = 0; y < dh; ++y) {
        int sy = oy + (int)((uint64_t)y * (uint64_t)crop_h / (uint64_t)dh);
        if (sy >= sh) sy = sh - 1;
        uint32_t* dst = (uint32_t*)(g_back + (size_t)y * g_buf_pitch);
        const uint16_t* row = src + (size_t)sy * sw;
        for (int x = 0; x < dw; ++x) {
            int sx = ox + (int)((uint64_t)x * (uint64_t)crop_w / (uint64_t)dw);
            if (sx >= sw) sx = sw - 1;
            uint16_t p = row[sx];
            uint8_t r = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
            uint8_t g = (uint8_t)(((p >> 5)  & 0x3F) * 255 / 63);
            uint8_t b = (uint8_t)((p & 0x1F) * 255 / 31);
            dst[x] = pack_rgb(r, g, b);
        }
    }
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
    if (rb & 3u) {   // 非 4 字节对齐: 退回逐字节 (罕见)
        for (int y = 0; y < g_buf_h; y++) {
            uint8_t* r0 = f + (size_t)y * rb;
            const uint8_t* r1 = b + (size_t)y * rb;
            int same = 1;
            for (size_t i = 0; i < rb; i++)
                if (r0[i] != r1[i]) { same = 0; break; }
            if (same) continue;
            for (size_t i = 0; i < rb; i++) r0[i] = r1[i];
        }
        return;
    }
    // 以 32 位字比较/拷贝: 比较循环迭代次数降为 1/4 (TCG 下整帧开销显著下降)
    const size_t words = rb >> 2;
    for (int y = 0; y < g_buf_h; y++) {
        uint32_t* r0 = (uint32_t*)(f + (size_t)y * rb);
        const uint32_t* r1 = (const uint32_t*)(b + (size_t)y * rb);
        int same = 1;
        for (size_t i = 0; i < words; i++)
            if (r0[i] != r1[i]) { same = 0; break; }
        if (same) continue;
        for (size_t i = 0; i < words; i++) r0[i] = r1[i];
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
        // 第一性原则: 桌面输出必须无黑边。旧实现把 320x200 等比缩放后
        // 居中，1920x1080 会留下约 96px 的左右黑边。兼容镜像路径现在直接
        // 覆盖整个 GOP 画布；正常的 UEFI/VBE LFB 路径不会进入此镜像器。
        g_gop_disp_w = g_gop_w;
        g_gop_disp_h = g_gop_h;
        g_gop_offx_i = 0;
        g_gop_offy_i = 0;
        g_gop_stepx  = (uint32_t)((320ULL << 16) / g_gop_disp_w);
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
