// wm.c - FSOS（自由安全操作系统）桌面环境（Aurora：macOS 主导的轻量桌面）
//
// 特性:
//   - 优先使用 UEFI/VBE 原生帧缓冲，桌面布局以实际分辨率为设计基准
//   - 蓝紫夜色壁纸 + 悬浮 Dock / 应用程序面板（纯色分层模拟玻璃感）
//   - 原创几何图标 (不照搬 Windows 图标) + 中文标签
//   - 桌面图标 (双击打开) + 网格化开始菜单 (单击启动)
//   - 窗口: 标题栏(可拖动)、最小化/最大化/关闭按钮、z 序、焦点
//   - 真实 RTC 时钟读 CMOS, 任务栏显示 HH:MM
//   - 快捷键/帮助覆盖层已中文化
//
#include "wm.h"
#include "gfx.h"
#include "vga.h"
#include "mouse.h"
#include "kb.h"
#include "user.h"
#include "terminal.h"
#include "taskmgr.h"
#include "lang.h"
#include "devstudio.h"
#include "vscode.h"
#include "editor.h"
#include "filemgr.h"
#include "disktool.h"
#include "sysconf.h"
#include "idt.h"
#include "mp_entry.h"
#include "cjk.h"
#include "io.h"
#include "theme.h"    // ui_polish: 圆角半径/对比组合集中声明
#include "window.h"    // gui_framework Phase 3: 窗口管理器 (app_t, wm_draw_window 等)
#include "compositor.h" // gui_framework Phase 3: 合成器
#include "event_queue.h" // gui_framework Phase 3: 事件队列
#include "safe_callback.h" // gui_framework Phase 3: 应用回调异常隔离
#include "shell.h"       // gui_framework Phase 4: 桌面外壳
#include "taskbar.h"     // gui_framework Phase 5: 任务栏
#include "startmenu.h"
#include "sidebar.h"
#include "animation.h"   // gui_framework Phase 6: 开始菜单
#include "aurora_wallpaper.h"
#include "linux.h"     // Linuxulator: Ctrl+Alt+L 运行 FS 中的 Linux ELF (HELLO.ELF)
// text2x() 时钟大数字从内核内置字体取字形 (与 vga/gfx/cjk 一致, UEFI 下 0xB0000 不可靠)
#include "boot/uefi/font8x8.h"

// 桌面用动态分辨率 (vga.h 的 VGA_W/SCREEN_H), 适配 mode13h 320x200 或 VBE 高分辨率
#define SCREEN_W   VGA_W
// 布局常量已迁移至 window.h (WM_TASKBAR_H/WM_TASKBAR_Y/WM_TITLE_H), 保留兼容别名
#define TASKBAR_H  WM_TASKBAR_H
#define TASKBAR_Y  WM_TASKBAR_Y
#define TITLE_H    WM_TITLE_H

static void d_about(int x, int y, int w, int h);
static void d_users(int x, int y, int w, int h);
static void d_clock(int x, int y, int w, int h);
static void d_settings(int x, int y, int w, int h);
static int  d_settings_mouse(int mx, int my, int ldown);

// ---- "开发"软件分发: 在 内置 DevStudio 与 宿主 VSCode 之间切换 ----
// 偏好存于 sysconf (dev_app), 默认内置 DevStudio。按 V(DevStudio) / D(VSCode) 切换。
static void dev_open(void) {
    if (sysconf_dev_app() == DEV_APP_VSCODE) vscode_open();
    else devstudio_open();
}
static void dev_draw(int x, int y, int w, int h) {
    if (sysconf_dev_app() == DEV_APP_VSCODE) vscode_draw(x, y, w, h);
    else devstudio_draw(x, y, w, h);
}
static int dev_key(int k) {
    if (sysconf_dev_app() == DEV_APP_VSCODE) return vscode_key(k);
    return devstudio_key(k);
}
static int dev_take_skip(void) {
    if (sysconf_dev_app() == DEV_APP_VSCODE) return vscode_take_skip();
    return devstudio_take_skip();
}
// VSCode 模式下, 鼠标 / 周期性重绘(光标闪烁) 转发给原生编辑器
static int dev_on_mouse(int mx, int my, int ldown) {
    if (sysconf_dev_app() == DEV_APP_VSCODE) return editor_on_mouse(mx, my, ldown);
    return devstudio_on_mouse(mx, my, ldown);
}
static int dev_tick(void) {
    if (sysconf_dev_app() == DEV_APP_VSCODE) return editor_tick();
    return 0;
}

// g_app 定义在 wm.c (引用 wm.c 内部 static 函数), extern 供 window.c 访问
app_t g_app[NAPP] = {
    { "关于",     8,   8, 204, 112, 1,0,0,  8,  8,204,112, 1, d_about,  0 },
    { "用户",   126,  16, 184, 140, 0,0,0, 126,16,184,140, 0, d_users,  0 },
    { "时钟",   146,  56, 148, 108, 0,0,0, 146,56,148,108, 0, d_clock,  0 },
    { "设置",    44,   8, 196, 152, 0,0,0,  44, 8,196,152, 0, d_settings, 0, d_settings_mouse },
    // 开发: 统一编辑 Python / C-C++ / Java 工程文件 (可在 DevStudio / VSCode 间切换)
    { "开发",     6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, dev_draw, dev_key, dev_on_mouse, dev_tick, 0 },
    // FSOS 原生 VSCode 风格代码编辑器 (多标签/侧边栏/语法高亮/命令面板/鼠标交互)
    { "编辑器",    6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, editor_draw, editor_key, editor_on_mouse, editor_tick, 0 },
    // 文件管理器: 浏览/打开/新建/删除/改名 + 磁盘信息
    { "文件管理器", 6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, filemgr_draw, filemgr_key, filemgr_on_mouse, 0, filemgr_on_rmouse },
    // 磁盘工具: 磁盘几何 / 逻辑分区 / 格式化文件系统
    { "磁盘工具",   6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, disktool_draw, disktool_key, disktool_on_mouse, 0, 0 },
    // 原生终端：真正的 WM app，不阻塞桌面主循环。
    { "终端",       6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, terminal_draw, terminal_key, terminal_on_mouse, terminal_on_tick, 0 },
};

// gui_framework Phase 3: g_maxz/g_focus/g_drag/g_dox/g_doy 已迁移至 window.c
// 读访问通过兼容宏, 写访问通过 wm_set_*/wm_next_z 函数
#define g_focus   (wm_get_focus())
#define g_maxz    (wm_get_maxz())
#define g_drag    (wm_get_drag())
#define g_dox     (wm_get_dox())
#define g_doy     (wm_get_doy())

static int   g_gfx_inited = 0;
// gui_framework Phase 4: 桌面状态 (extern 供 shell.c)
int   g_wallpaper_palette_ready = 0;

int   g_start_open = 0;
int   g_sel_icon = -1;
static uint32_t g_last_click_t = 0;
static int   g_last_click_icon = -1;
int   g_show_help = 0;
static int   g_wm_skip = 0;
static int   g_wm_quit = 0;

// 右键上下文菜单
int   g_ctx_open = 0;
int   g_ctx_x = 0, g_ctx_y = 0;
int   g_force_redraw = 0;
const ctx_entry_t g_ctx[] = {
    { "打开终端", ACT_TERMINAL },
    { "文件管理器", 6 },
    { "编辑器",   5 },
    { "磁盘工具", 7 },
    { "刷新桌面", -99 },
    { "关于本机", 0 },
    { "关机",     -98 },
    { "重启",     -97 },
};
#define NCTX (sizeof(g_ctx)/sizeof(g_ctx[0]))
const int g_nctx = sizeof(g_ctx)/sizeof(g_ctx[0]);

// 新手提示: 进入桌面后显示 8 秒
int   g_tip_visible = 1;
uint32_t g_tip_until = 0;

// 开始菜单项 (9 项, 3x3 网格)
const startmenu_entry_t g_start[] = {
    { "关于",    0,           15 },
    { "用户",    1,           1 },
    { "时钟",    2,           13 },
    { "设置",    3,           14 },
    { "终端",    ACT_TERMINAL, 4 },
    { "Python",  ACT_PYTHON,   18 },
    { "C/C++",   ACT_CC,       6 },
    { "Java",    ACT_JAVA,     7 },
    { "开发",    4,            9 },
    { "编辑器",  5,            6 },
    { "文件管理器", 6,         10 },
    { "磁盘工具", 7,           11 },
    { "注销",    ACT_LOGOFF,   8 },
};
#define NSTART (sizeof(g_start)/sizeof(g_start[0]))
const int g_nstart = (int)(sizeof(g_start)/sizeof(g_start[0]));

// 桌面图标 (2 列 x 4 行)
const icon_entry_t g_icons[] = {
    { "此电脑",     6,            2 },
    { "回收站",    5,           12 },
    { "文档",       6,           17 },
    { "FSOS 应用", ACT_START,    16 },
};
#define NICON (sizeof(g_icons)/sizeof(g_icons[0]))
const int g_nicon = (int)(sizeof(g_icons)/sizeof(g_icons[0]));

// ============================================================
// 基础绘制助手
// ============================================================
static void fill(int x0,int y0,int x1,int y1,uint8_t c){
    if (gfx_clip_active() && !gfx_clip_intersects(x0,y0,x1,y1)) return;   // 脏区局部重绘: 整段跳过
    gfx_fill_idx(x0,y0,x1,y1,c);
}
static void rect(int x0,int y0,int x1,int y1,uint8_t c){
    if (gfx_clip_active() && !gfx_clip_intersects(x0,y0,x1,y1)) return;
    gfx_rect_idx(x0,y0,x1,y1,c);
}
static void txt(int x,int y,const char* s,uint8_t f,uint8_t b){ cjk_text(x,y,s,f,b); }
static void fill_round(int x0,int y0,int x1,int y1,int r,uint8_t c){
    if (gfx_clip_active() && !gfx_clip_intersects(x0,y0,x1,y1)) return;
    if (gfx_is_lfb()) {
        uint8_t cr,cg,cb; gfx_idx_rgb(c,&cr,&cg,&cb);
        gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,cr,cg,cb);
    } else gfx_fill_round_idx(x0,y0,x1,y1,r,c);
}
static void rect_round(int x0,int y0,int x1,int y1,int r,uint8_t c){
    if (gfx_clip_active() && !gfx_clip_intersects(x0,y0,x1,y1)) return;
    if (gfx_is_lfb()) {
        uint8_t cr,cg,cb; gfx_idx_rgb(c,&cr,&cg,&cb);
        gfx_round_rect_rgb_aa(x0,y0,x1,y1,r,cr,cg,cb);
    } else gfx_round_rect_idx(x0,y0,x1,y1,r,c);
}

// 8x8 字体放大 2 倍绘制 ASCII (用于时钟大数字)
static void text2x(int x, int y, const char* s, uint8_t f, uint8_t b) {
    const uint8_t* font = (const uint8_t*)g_font8x8;   // 内置字体, 按 (ch-0x20)*8 索引 (与 vga/gfx 一致)
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 0x20) { x += 16; continue; }
        if (ch >= 128) ch = '?';
        const uint8_t* g = font + (uint32_t)(ch - 0x20) * 8;
        // font8x8.h: petme128 每字节一列, bit0=最上
        for (int col = 0; col < 8; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < 8; row++) {
                int on = (bits >> row) & 1;
                int c = on ? f : b;
                fill(x + col*2, y + row*2, x + col*2 + 1, y + row*2 + 1, c);
            }
        }
        x += 16;
    }
}

// 鼠标指针: 45° 箭头, 白身 + 黑色描边(右下外扩). 热点 (x,y) 在箭头尖端.
// (旧版纯白单线指针在白色窗口/任务栏上几乎不可见, 是"指针不好操作"的主要观感来源;
//  黑描边保证浅色深色背景都清晰.)
static void draw_cursor(int x, int y) {
    // 经典箭头指针：黑色双像素描边 + 白色主体；热点 (x,y) 为箭头尖端。
    // 指针始终在完整合成帧的最后绘制，旧位置由下一帧的 root scene 覆盖。
    static const uint8_t outer[16] = {2,3,4,5,6,7,8,9,10,11,12,13,11,9,8,7};
    static const uint8_t inner[16] = {1,2,3,4,5,6,7,8,9,10,11,12,9,7,6,5};
    int sc = (VGA_W >= 1400) ? 2 : 1;
    int maxx=VGA_W-1,maxy=SCREEN_H-1;
    for(int r=0;r<16;r++){
        int xo=x, yo=y+r*sc;
        int ow=outer[r]*sc, iw=inner[r]*sc;
        if(xo>maxx||yo>maxy)continue;
        int ox1=xo+ow-1; if(ox1>maxx)ox1=maxx;
        int oy1=yo+sc-1; if(oy1>maxy)oy1=maxy;
        gfx_fill_idx(xo,yo,ox1,oy1,COL_BLACK);
        if(iw>0){int ix1=xo+iw-1;if(ix1>maxx)ix1=maxx;gfx_fill_idx(xo,yo,ix1,oy1,COL_WHITE);}
    }
}

// ============================================================
// 原创几何图标 (24x20 画布, 彩色方块底 + 白色图形)
// kind 0..8: 关于/用户/时钟/设置/终端/Python/C/Java/注销
// ============================================================
static void rect_square(int x,int y,int w,int h,uint8_t c) { rect(x,y,x+w-1,y+h-1,c); }

// 填充圆 (近似)
static void fcircle(int cx, int cy, int r, uint8_t c) {
    if (gfx_is_lfb()) {
        uint8_t cr,cg,cb; gfx_idx_rgb(c,&cr,&cg,&cb);
        gfx_disc_aa(cx,cy,r,cr,cg,cb);
    } else {
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx*dx + dy*dy <= r*r)
                    gfx_pixel_idx(cx + dx, cy + dy, c);
    }
}
// 圆环 (内外半径)
static void fring(int cx, int cy, int r, int t, uint8_t c) {
    int r2 = r*r, r1 = (r - t)*(r - t);
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 <= r2 && d2 >= r1) gfx_pixel_idx(cx + dx, cy + dy, c);
        }
}

// gui_framework Phase 4: draw_icon 公开供 shell.c 使用
static uint8_t icon_bg_for_kind(int kind) {
    switch (kind) {
        case 0:  return COL_UI_DOCK_HI_SOFT;
        case 1:  return COL_ACCENT;
        case 2:  return COL_ACCENT_SOFT;
        case 3:  return COL_UI_BG_SOFT;
        case 4:  return COL_BLACK;
        case 5:  return COL_ACCENT;
        case 6:  return COL_ORANGE;
        case 7:  return COL_BROWN;
        case 8:  return COL_LRED;
        case 9:  return COL_LGREEN;
        case 10: return COL_YELLOW;
        case 11: return COL_DGRAY;
        case 12: return COL_LGRAY;        // 回收站: 银灰 (原 COL_PANEL_HI 6-bit 越界偏紫)
        case 13: return COL_ACCENT;       // 时钟
        case 14: return COL_DGRAY;        // 设置
        case 15: return COL_ACCENT;       // 关于
        case 16: return COL_ACCENT;       // FSOS 应用
        case 17: return COL_ACCENT;       // 文档
        case 18: return COL_ACCENT;       // Python
        default: return COL_UI_BG_SOFT;
    }
}

// 图标底色真彩 (macOS Big Sur 风格主色): 仅 LFB 路径使用
static void icon_rgb_for_kind(int kind, uint8_t* R, uint8_t* G, uint8_t* B) {
    switch (kind) {
        case 0:  *R=0x33; *G=0x3A; *B=0x45; break;  // 终端 深板岩
        case 1:  *R=0x6E; *G=0x9B; *B=0xEA; break;  // 用户 蓝
        case 2:  *R=0x3B; *G=0x86; *B=0xF2; break;  // 此电脑 蓝
        case 3:  *R=0x5A; *G=0x6B; *B=0x82; break;  // 任务管理 灰蓝
        case 4:  *R=0x2B; *G=0x30; *B=0x3A; break;  // 锁定 深灰
        case 5:  *R=0x4E; *G=0x8D; *B=0xEF; break;  // 软件 蓝
        case 6:  *R=0xE8; *G=0x7A; *B=0x3C; break;  // 代码 橙
        case 7:  *R=0xB2; *G=0x7A; *B=0x45; break;  // Java 棕
        case 8:  *R=0xE3; *G=0x53; *B=0x4B; break;  // 电源 红
        case 9:  *R=0x38; *G=0xB5; *B=0x6E; break;  // 开发 绿
        case 10: *R=0x4C; *G=0x9C; *B=0xEF; break;  // 文件夹 蓝
        case 11: *R=0x5C; *G=0x69; *B=0x7B; break;  // 磁盘 灰
        case 12: *R=0xC6; *G=0xCF; *B=0xD9; break;  // 回收站 银灰
        case 13: *R=0x3B; *G=0x86; *B=0xF2; break;  // 时钟 蓝
        case 14: *R=0x7E; *G=0x8A; *B=0x9C; break;  // 设置 灰
        case 15: *R=0x3B; *G=0x86; *B=0xF2; break;  // 关于 蓝
        case 16: *R=0x2E; *G=0x74; *B=0xE6; break;  // FSOS 应用 蓝
        case 17: *R=0x5A; *G=0xA9; *B=0xF0; break;  // 文档 浅蓝
        case 18: *R=0x3B; *G=0x86; *B=0xF2; break;  // Python 蓝
        default: *R=0x5A; *G=0x6B; *B=0x82; break;
    }
}

// 渐变圆角底: LFB 用真彩 + 自上而下递减的白色光泽 (玻璃质感); mode13h 回退纯色
static void tile_bg(int x, int y, int size, int r, uint8_t idx,
                    uint8_t R, uint8_t G, uint8_t B) {
    if (gfx_is_lfb()) {
        gfx_fill_round_rgb_aa(x, y, x + size - 1, y + size - 1, r, R, G, B);
        // 顶部柔光: 由下至上叠加三层低透明度白色 => 平滑渐变, 顶部更亮
        gfx_fill_round_rgb_alpha(x, y, x + size - 1, y + size * 76 / 100, r, 255, 255, 255, 18);
        gfx_fill_round_rgb_alpha(x, y, x + size - 1, y + size * 50 / 100, r, 255, 255, 255, 18);
        gfx_fill_round_rgb_alpha(x, y, x + size - 1, y + size * 26 / 100, r, 255, 255, 255, 18);
    } else {
        fill_round(x, y, x + size - 1, y + size - 1, r, idx);
    }
}

static void icon_line(int x0,int y0,int x1,int y1,uint8_t c){
    uint8_t r,g,b; gfx_idx_rgb(c,&r,&g,&b);
    if(gfx_is_lfb()) gfx_line_aa(x0,y0,x1,y1,r,g,b); else gfx_pixel_idx(x1,y1,c);
}

// 真彩版本的圆角填充 / 线条: LFB 用 (R,G,B), 8bpp 回退到给定索引
static void icon_rfill(int x0,int y0,int x1,int y1,int rad,
                       uint8_t R,uint8_t G,uint8_t B,uint8_t idx){
    if(gfx_is_lfb()) gfx_fill_round_rgb_aa(x0,y0,x1,y1,rad,R,G,B);
    else fill_round(x0,y0,x1,y1,rad,idx);
}
static void icon_rline(int x0,int y0,int x1,int y1,
                       uint8_t R,uint8_t G,uint8_t B,uint8_t idx){
    if(gfx_is_lfb()) gfx_line_aa(x0,y0,x1,y1,R,G,B); else gfx_pixel_idx(x1,y1,idx);
}
// 真彩圆盘: LFB 直接画, 8bpp 用索引近似
static void icon_rdisc(int cx,int cy,int rad,uint8_t R,uint8_t G,uint8_t B,uint8_t idx){
    if(gfx_is_lfb()) gfx_disc_aa(cx,cy,rad,R,G,B); else fcircle(cx,cy,rad,idx);
}

void draw_icon_big(int kind, int x, int y, int size) {
    if(size<24) size=24;
    if (gfx_clip_active() && !gfx_clip_intersects(x, y, x + size - 1, y + size - 1)) return;  // 整图标跳过
    int r=size/5, cx=x+size/2, cy=y+size/2;
    uint8_t tile=icon_bg_for_kind(kind);
    uint8_t fg=theme_get_color_idx(COLOR_FG_TITLE);
    uint8_t soft=theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t accent=theme_get_color_idx(COLOR_ACCENT);
    uint8_t ok=theme_get_color_idx(COLOR_SUCCESS);
    uint8_t warn=theme_get_color_idx(COLOR_WARNING);
    uint8_t danger=theme_get_color_idx(COLOR_DANGER);

    // 图标主色 (真彩) + 深色挖空色; 以及常用语义色的真彩值
    uint8_t R,G,B; icon_rgb_for_kind(kind,&R,&G,&B);
    uint8_t dR=(uint8_t)(R*52/100), dG=(uint8_t)(G*52/100), dB=(uint8_t)(B*52/100);
    uint8_t wR,wG,wB; gfx_idx_rgb(fg,&wR,&wG,&wB);        // 白
    uint8_t aR,aG,aB; gfx_idx_rgb(accent,&aR,&aG,&aB);    // 强调蓝
    uint8_t sR,sG,sB; gfx_idx_rgb(soft,&sR,&sG,&sB);      // 灰

    tile_bg(x,y,size,r,tile,R,G,B);
    if(gfx_is_lfb()){
        uint8_t rr,gg,bb; gfx_idx_rgb(theme_get_color_idx(COLOR_BORDER),&rr,&gg,&bb);
        gfx_round_rect_rgb_aa(x,y,x+size-1,y+size-1,r,rr,gg,bb);
    } else rect_round(x,y,x+size-1,y+size-1,r,theme_get_color_idx(COLOR_BORDER));

    int ix=x+size/6, iy=y+size/6, iw=size*2/3;
    switch(kind){
    case 1: /* 用户 */
        icon_rdisc(cx,y+size*31/100,size/10,wR,wG,wB,fg);
        icon_rfill(x+size*27/100,y+size*51/100,x+size*73/100,y+size*78/100,size/5,wR,wG,wB,fg);
        break;
    case 2: /* 此电脑: 白色显示器 + 深色屏幕 */
        icon_rfill(x+size*17/100,y+size*18/100,x+size*83/100,y+size*67/100,size/8,wR,wG,wB,fg);
        icon_rfill(x+size*24/100,y+size*26/100,x+size*76/100,y+size*60/100,size/12,0x18,0x2A,0x46,COL_SHADOW);
        icon_rline(cx,y+size*67/100,cx,y+size*79/100,wR,wG,wB,fg);
        icon_rline(x+size*35/100,y+size*79/100,x+size*65/100,y+size*79/100,wR,wG,wB,fg);
        break;
    case 4: /* 终端 */
        icon_rfill(ix,iy+size/12,ix+iw-1,iy+size*58/100,r/3,0x14,0x18,0x1F,COL_BLACK);
        rect_round(ix,iy+size/12,ix+iw-1,iy+size*58/100,r/3,soft);
        icon_rline(ix+size*20/100,iy+size*25/100,ix+size*34/100,iy+size*37/100,aR,aG,aB,accent);
        icon_rline(ix+size*34/100,iy+size*37/100,ix+size*20/100,iy+size*49/100,aR,aG,aB,accent);
        icon_rline(ix+size*43/100,iy+size*50/100,ix+size*63/100,iy+size*50/100,0x5A,0xD6,0x8A,ok);
        break;
    case 3: /* 任务管理 */
        { uint8_t oR,oG,oB, xR,xG,xB; gfx_idx_rgb(ok,&oR,&oG,&oB); gfx_idx_rgb(danger,&xR,&xG,&xB);
          icon_rfill(x+size*22/100,y+size*53/100,x+size*32/100,y+size*78/100,r/4,oR,oG,oB,ok);
          icon_rfill(x+size*37/100,y+size*42/100,x+size*47/100,y+size*78/100,r/4,0xF2,0xB0,0x4C,warn);
          icon_rfill(x+size*52/100,y+size*30/100,x+size*62/100,y+size*78/100,r/4,xR,xG,xB,danger); }
        break;
    case 5: /* 软件 */
        icon_rfill(x+size*22/100,y+size*22/100,x+size*78/100,y+size*78/100,size/6,wR,wG,wB,fg);
        icon_rfill(x+size*38/100,y+size*38/100,x+size*62/100,y+size*62/100,size/8,aR,aG,aB,accent);
        icon_rline(x+size*32/100,y+size*32/100,x+size*68/100,y+size*68/100,dR,dG,dB,tile);
        break;
    case 6: /* 代码 */
        icon_rline(x+size*35/100,y+size*27/100,x+size*22/100,cy,wR,wG,wB,fg);
        icon_rline(x+size*22/100,cy,x+size*35/100,y+size*73/100,wR,wG,wB,fg);
        icon_rline(x+size*65/100,y+size*27/100,x+size*78/100,cy,wR,wG,wB,fg);
        icon_rline(x+size*78/100,cy,x+size*65/100,y+size*73/100,wR,wG,wB,fg);
        icon_rline(cx,y+size*22/100,cx-size/14,y+size*78/100,aR,aG,aB,accent);
        break;
    case 7: /* Java */
        icon_rfill(x+size*26/100,y+size*43/100,x+size*68/100,y+size*73/100,size/8,wR,wG,wB,fg);
        icon_rfill(x+size*61/100,y+size*34/100,x+size*76/100,y+size*58/100,size/10,wR,wG,wB,fg);
        icon_rline(x+size*35/100,y+size*27/100,x+size*60/100,y+size*27/100,sR,sG,sB,soft);
        icon_rline(x+size*35/100,y+size*33/100,x+size*55/100,y+size*33/100,sR,sG,sB,soft);
        break;
    case 8: /* 电源 */
        fring(cx,cy+size/8,size/5,size/16,fg);
        icon_rfill(cx-size/24,y+size*18/100,cx+size/24,y+size*57/100,size/24,wR,wG,wB,fg);
        break;
    case 9: /* 开发 */
        icon_rline(x+size*27/100,y+size*35/100,cx-size/7,cy,wR,wG,wB,fg);
        icon_rline(cx-size/7,cy,x+size*27/100,y+size*65/100,wR,wG,wB,fg);
        icon_rline(x+size*73/100,y+size*35/100,cx+size/7,cy,wR,wG,wB,fg);
        icon_rline(cx+size/7,cy,x+size*73/100,y+size*65/100,wR,wG,wB,fg);
        icon_rfill(cx-size/12,cy-size/12,cx+size/12,cy+size/12,size/12,wR,wG,wB,fg);
        break;
    case 10: /* 文件夹 */
        icon_rfill(x+size*14/100,y+size*29/100,x+size*50/100,y+size*43/100,size/9,wR,wG,wB,fg);
        icon_rfill(x+size*14/100,y+size*36/100,x+size*86/100,y+size*77/100,size/9,wR,wG,wB,fg);
        icon_rfill(x+size*14/100,y+size*35/100,x+size*86/100,y+size*70/100,size/9,0x8F,0xC7,0xFF,accent);
        break;
    case 11: /* 磁盘 */
        icon_rfill(x+size*20/100,y+size*25/100,x+size*80/100,y+size*75/100,size/8,wR,wG,wB,fg);
        icon_rfill(x+size*30/100,y+size*31/100,x+size*70/100,y+size*39/100,size/12,sR,sG,sB,soft);
        icon_rfill(x+size*45/100,y+size*50/100,x+size*55/100,y+size*60/100,size/12,aR,aG,aB,accent);
        break;
    case 12: /* 回收站: 垃圾桶 (盖/提手/桶身/竖纹) */
        icon_rline(x+size*24/100,y+size*28/100,x+size*76/100,y+size*28/100,wR,wG,wB,fg);       // 盖沿
        icon_rfill(x+size*41/100,y+size*19/100,x+size*59/100,y+size*26/100,size/20,wR,wG,wB,fg); // 提手
        icon_rfill(x+size*29/100,y+size*31/100,x+size*71/100,y+size*81/100,size/10,wR,wG,wB,fg); // 桶身
        icon_rline(x+size*43/100,y+size*40/100,x+size*45/100,y+size*71/100,dR,dG,dB,tile);       // 竖纹
        icon_rline(x+size*57/100,y+size*40/100,x+size*55/100,y+size*71/100,dR,dG,dB,tile);
        break;
    case 13: /* 时钟: 表盘 + 指针 */
        icon_rdisc(cx,cy,size*40/100,sR,sG,sB,soft);
        icon_rline(cx,cy,cx,cy-size*26/100,wR,wG,wB,fg);
        icon_rline(cx,cy,cx+size*20/100,cy+size*12/100,wR,wG,wB,fg);
        icon_rdisc(cx,cy,size*7/100,aR,aG,aB,accent);
        break;
    case 14: /* 设置: 齿轮 (圆盘 + 8 齿 + 轴孔) */
        { int d=size*33/100, d2=d*71/100, tr=size*9/100;
          icon_rdisc(cx,    cy-d, tr,wR,wG,wB,fg);
          icon_rdisc(cx+d2, cy-d2,tr,wR,wG,wB,fg);
          icon_rdisc(cx+d,  cy,   tr,wR,wG,wB,fg);
          icon_rdisc(cx+d2, cy+d2,tr,wR,wG,wB,fg);
          icon_rdisc(cx,    cy+d, tr,wR,wG,wB,fg);
          icon_rdisc(cx-d2, cy+d2,tr,wR,wG,wB,fg);
          icon_rdisc(cx-d,  cy,   tr,wR,wG,wB,fg);
          icon_rdisc(cx-d2, cy-d2,tr,wR,wG,wB,fg);
          icon_rdisc(cx,cy,size*29/100,wR,wG,wB,fg);
          icon_rdisc(cx,cy,size*12/100,R,G,B,dR); }   // 轴孔 (透出底色)
        break;
    case 15: /* 关于: 信息 i */
        icon_rdisc(cx,cy,size*38/100,aR,aG,aB,accent);
        icon_rfill(cx-size*4/100,cy+size*2/100,cx+size*4/100,cy+size*24/100,size/12,wR,wG,wB,fg);
        icon_rdisc(cx,cy-size*18/100,size*6/100,wR,wG,wB,fg);
        break;
    case 16: /* FSOS 应用 / 启动台: 3x3 圆点 */
        for(int rr=0;rr<3;rr++)
            for(int cc=0;cc<3;cc++){
                int dx=x+size*(33+cc*17)/100;
                int dy=y+size*(33+rr*17)/100;
                icon_rdisc(dx,dy,size*6/100,wR,wG,wB,fg);
            }
        break;
    case 17: /* 文档: 白页 + 折角 + 文本行 */
        icon_rfill(x+size*25/100,y+size*15/100,x+size*75/100,y+size*85/100,size/10,wR,wG,wB,fg);
        icon_rline(x+size*60/100,y+size*15/100,x+size*75/100,y+size*30/100,aR,aG,aB,accent);   // 折角
        icon_rfill(x+size*25/100,y+size*15/100,x+size*61/100,y+size*29/100,size/10,wR,wG,wB,fg);
        for(int rr=0;rr<3;rr++){
            int yy=y+size*(43+rr*13)/100;
            icon_rline(x+size*32/100,yy,x+size*68/100,yy,sR,sG,sB,soft);
        }
        break;
    case 18: /* Python: 蓝色底 + 黄色 P 形 */
        icon_rfill(x+size*30/100,y+size*22/100,x+size*42/100,y+size*80/100,size/10,0xE8,0xB4,0x3C,warn);
        icon_rfill(x+size*30/100,y+size*22/100,x+size*72/100,y+size*48/100,size/8,0xE8,0xB4,0x3C,warn);
        icon_rfill(x+size*42/100,y+size*30/100,x+size*62/100,y+size*40/100,size/12,dR,dG,dB,tile);
        break;
    default:
        icon_rdisc(cx,cy,size/5,wR,wG,wB,fg);
        break;
    }
}

void draw_icon(int kind, int x, int y) {
    draw_icon_big(kind, x, y, 26);
}

// ============================================================
// 应用窗口内容
// ============================================================
static void d_about(int x, int y, int w, int h) {
    uint8_t panel = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t field = theme_get_color_idx(COLOR_FIELD);
    uint8_t fg = theme_get_color_idx(COLOR_FG);
    uint8_t soft = theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t accent = theme_get_color_idx(COLOR_ACCENT);
    int pad = theme_get_padding(PADDING_PANEL);
    int card_h = 40 * THEME_SF;
    if (card_h > h - 12 * THEME_SF) card_h = h - 12 * THEME_SF;
    fill_round(x + pad, y + pad, x + w - pad - 1, y + pad + card_h,
               theme_get_radius(RADIUS_CTRL_T), field);
    fill_round(x + pad + 7 * THEME_SF, y + pad + 7 * THEME_SF,
               x + pad + 27 * THEME_SF, y + pad + 27 * THEME_SF,
               7 * THEME_SF, accent);
    txt(x + pad + 12 * THEME_SF, y + pad + 10 * THEME_SF, "FS", fg, accent);
    txt(x + pad + 34 * THEME_SF, y + pad + 4 * THEME_SF, "FSOS", fg, field);
    txt(x + pad + 34 * THEME_SF, y + pad + 19 * THEME_SF, "现代原生桌面", soft, field);
    int yy = y + pad + card_h + 7 * THEME_SF;
    txt(x + pad, yy, "Aurora Desktop", accent, panel);
    txt(x + pad, yy + 14 * THEME_SF, "x86-64 - UEFI - 原生 GUI", soft, panel);
    txt(x + pad, yy + 28 * THEME_SF, "窗口、Dock、开始菜单统一设计", soft, panel);
}

static void d_users(int x, int y, int w, int h) {
    (void)h;
    uint8_t panel = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t field = theme_get_color_idx(COLOR_FIELD);
    uint8_t fg = theme_get_color_idx(COLOR_FG);
    uint8_t soft = theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t accent = theme_get_color_idx(COLOR_ACCENT);
    int cnt = user_count();
    int row_h = 22 * THEME_SF;
    int pad = theme_get_padding(PADDING_PANEL);
    txt(x + pad, y + 2 * THEME_SF, "账户", fg, panel);
    txt(x + w - pad - 28 * THEME_SF, y + 2 * THEME_SF, "管理", accent, panel);
    for (int i = 0; i < cnt && i < 5; i++) {
        const UserRec* u = user_get(i);
        if (!u) break;
        int ry = y + 16 * THEME_SF + i * row_h;
        fill_round(x + pad, ry, x + w - pad - 1, ry + row_h - 3 * THEME_SF,
                   theme_get_radius(RADIUS_CTRL_T), field);
        fill_round(x + pad + 4 * THEME_SF, ry + 4 * THEME_SF,
                   x + pad + 16 * THEME_SF, ry + 16 * THEME_SF,
                   6 * THEME_SF, (u->role >= ROLE_ADMIN) ? accent : theme_get_color_idx(COLOR_ACCENT_SOFT));
        char line[32];
        int lp = 0;
        for (int c = 0; u->name[c] && lp < 20; c++) line[lp++] = u->name[c];
        line[lp] = 0;
        txt(x + pad + 22 * THEME_SF, ry + 3 * THEME_SF, line, fg, field);
        const char* role = (u->role == ROLE_ROOT) ? "root" : ((u->role == ROLE_ADMIN) ? "管理员" : "普通用户");
        txt(x + pad + 22 * THEME_SF, ry + 11 * THEME_SF, role, soft, field);
    }
}

static void d_clock(int x, int y, int w, int h) {
    (void)h;
    uint8_t panel = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t field = theme_get_color_idx(COLOR_FIELD);
    uint8_t fg = theme_get_color_idx(COLOR_FG);
    uint8_t soft = theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t accent = theme_get_color_idx(COLOR_ACCENT);
    extern int rtc_h, rtc_m, rtc_s, rtc_ok;
    extern int rtc_y, rtc_mo, rtc_d;
    char buf[24];
    if (rtc_ok) {
        buf[0]=(char)('0'+rtc_h/10); buf[1]=(char)('0'+rtc_h%10); buf[2]=':';
        buf[3]=(char)('0'+rtc_m/10); buf[4]=(char)('0'+rtc_m%10); buf[5]=0;
    } else { buf[0]='-'; buf[1]='-'; buf[2]=':'; buf[3]='-'; buf[4]='-'; buf[5]=0; }
    fill_round(x + 8 * THEME_SF, y + 6 * THEME_SF,
               x + w - 8 * THEME_SF - 1, y + 44 * THEME_SF,
               theme_get_radius(RADIUS_CTRL_T), field);
    txt(x + 18 * THEME_SF, y + 13 * THEME_SF, buf, fg, field);
    if (rtc_ok) {
        int dp = 0;
        buf[dp++]='2'; buf[dp++]='0'; buf[dp++]=(char)('0'+rtc_y/10); buf[dp++]=(char)('0'+rtc_y%10);
        buf[dp++]='-'; buf[dp++]=(char)('0'+rtc_mo/10); buf[dp++]=(char)('0'+rtc_mo%10);
        buf[dp++]='-'; buf[dp++]=(char)('0'+rtc_d/10); buf[dp++]=(char)('0'+rtc_d%10); buf[dp]=0;
        txt(x + 18 * THEME_SF, y + 29 * THEME_SF, buf, soft, field);
    } else txt(x + 18 * THEME_SF, y + 29 * THEME_SF, "RTC 不可用", soft, field);
    uint32_t ms = get_ticks();
    uint32_t sec = ms / 1000u;
    int mm = (int)((sec / 60u) % 100u), ss = (int)(sec % 60u);
    buf[0]=(char)('0'+mm/10); buf[1]=(char)('0'+mm%10); buf[2]=':';
    buf[3]=(char)('0'+ss/10); buf[4]=(char)('0'+ss%10); buf[5]=0;
    txt(x + 8 * THEME_SF, y + 56 * THEME_SF, "运行时间", soft, panel);
    txt(x + 56 * THEME_SF, y + 56 * THEME_SF, buf, accent, panel);
}

static void d_settings(int x, int y, int w, int h) {
    (void)h;
    uint8_t panel = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t field = theme_get_color_idx(COLOR_FIELD);
    uint8_t fg = theme_get_color_idx(COLOR_FG);
    uint8_t soft = theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t accent = theme_get_color_idx(COLOR_ACCENT);
    int pad = theme_get_padding(PADDING_PANEL);
    txt(x + pad, y + 2 * THEME_SF, "系统设置", fg, panel);
    txt(x + pad, y + 16 * THEME_SF, "外观", accent, panel);
    fill_round(x + pad, y + 28 * THEME_SF, x + w - pad - 1, y + 48 * THEME_SF,
               theme_get_radius(RADIUS_CTRL_T), field);
    txt(x + pad + 8 * THEME_SF, y + 34 * THEME_SF, "深色主题", fg, field);
    txt(x + w - pad - 32 * THEME_SF, y + 34 * THEME_SF, theme_get_mode()==THEME_MODE_DARK?"深色":"浅色", accent, field);
    fill_round(x + pad, y + 54 * THEME_SF, x + w - pad - 1, y + 74 * THEME_SF,
               theme_get_radius(RADIUS_CTRL_T), field);
    txt(x + pad + 8 * THEME_SF, y + 60 * THEME_SF, "桌面动画", fg, field);
    txt(x + w - pad - 32 * THEME_SF, y + 60 * THEME_SF, anim_get_enabled()?"开启":"关闭", accent, field);
    txt(x + pad, y + 88 * THEME_SF, "Win+T  终端", soft, panel);
    txt(x + pad, y + 102 * THEME_SF, "Win+E  设置", soft, panel);
}

// T4.4: 设置面板鼠标交互 — 点击切换主题/动画
static int d_settings_mouse(int mx, int my, int ldown) {
    if (!ldown) return 0;
    app_t* a = &g_app[3];
    if (!a->open || a->minimized) return 0;
    int cx = a->x + 10, cy = a->y + WM_TITLE_H + 8;
    int cw = a->w - 20;
    int pad = theme_get_padding(PADDING_PANEL);
    int x0 = cx + pad, x1 = cx + cw - pad - 1;
    if (mx < x0 || mx > x1) return 0;
    int row1_y0 = cy + 28 * THEME_SF, row1_y1 = cy + 48 * THEME_SF;
    int row2_y0 = cy + 54 * THEME_SF, row2_y1 = cy + 74 * THEME_SF;
    if (my >= row1_y0 && my < row1_y1) {
        theme_set_mode(theme_get_mode() == THEME_MODE_DARK ? THEME_MODE_LIGHT : THEME_MODE_DARK);
        compositor_invalidate_all();
        return 1;
    }
    if (my >= row2_y0 && my < row2_y1) {
        anim_set_enabled(!anim_get_enabled());
        compositor_invalidate_all();
        return 1;
    }
    return 0;
}

// ============================================================
// RTC 时钟读取 (CMOS)
// ============================================================
int rtc_h = 0, rtc_m = 0, rtc_s = 0, rtc_ok = 0;
int rtc_y = 0, rtc_mo = 0, rtc_d = 0;

static uint8_t rtc_read_reg(uint8_t reg) {
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

static int bcd_valid(uint8_t v) {
    return ((v >> 4) <= 9) && ((v & 0x0F) <= 9);
}

// rtc_update 已公开 (taskbar.h 声明, gui_framework Phase 5)
void rtc_update(void) {
    // 等待 CMOS 不处于更新中
    for (int i = 0; i < 1000; i++) {
        if (!(rtc_read_reg(0x0A) & 0x80)) break;
    }
    uint8_t s  = rtc_read_reg(0x00);
    uint8_t mi = rtc_read_reg(0x02);
    uint8_t h  = rtc_read_reg(0x04);
    uint8_t d  = rtc_read_reg(0x07);
    uint8_t mo = rtc_read_reg(0x08);
    uint8_t y  = rtc_read_reg(0x09);
    // 再读一次秒, 若相同则稳定
    uint8_t s2 = rtc_read_reg(0x00);
    if (s != s2) { s = s2; }

    // BCD 转二进制 (假设寄存器为 BCD; VMware 通常为 BCD 或二进制, 这里按 BCD 处理)
    auto_bcd:
    if (!bcd_valid(s) || !bcd_valid(mi) || !bcd_valid(h) ||
        !bcd_valid(d) || !bcd_valid(mo) || !bcd_valid(y)) {
        rtc_ok = 0;
        return;
    }
    rtc_s  = ((s  >> 4) * 10) + (s  & 0x0F);
    rtc_m  = ((mi >> 4) * 10) + (mi & 0x0F);
    rtc_h  = ((h  >> 4) * 10) + (h  & 0x0F);
    rtc_d  = ((d  >> 4) * 10) + (d  & 0x0F);
    rtc_mo = ((mo >> 4) * 10) + (mo & 0x0F);
    rtc_y  = ((y  >> 4) * 10) + (y  & 0x0F);
    if (rtc_h < 24 && rtc_m < 60 && rtc_s < 60 && rtc_mo >= 1 && rtc_mo <= 12 && rtc_d >= 1 && rtc_d <= 31) {
        rtc_ok = 1;
    } else {
        rtc_ok = 0;
    }
}

// ============================================================
// 窗口框架 (最大化/还原/最小化/关闭)
// gui_framework Phase 3: draw_window/win_hit/round_rect_hit/win_btn_x
// 已迁移至 window.c, 保留兼容别名供 wm.c 内部使用
// ============================================================
#define draw_window    wm_draw_window
#define win_hit        wm_hit_test
#define round_rect_hit wm_round_rect_hit
#define win_btn_x      wm_win_btn_x

// ============================================================
// 应用生命周期
// ============================================================
static void close_app(int idx) {
    if (idx < 0 || idx >= NAPP) return;
    if (idx == 4) devstudio_close();
    else if (idx == 5) editor_close();
    else if (idx == 8) terminal_close();
    g_app[idx].open = 0;
    g_app[idx].minimized = 0;
    g_app[idx].maximized = 0;
    g_app[idx].z = 0;
}

// ============================================================
// 启动动作
// ============================================================
static int launch(int act) {
    if (act == ACT_LOGOFF) return 1;
    if (act == ACT_START) { g_start_open = 1; startmenu_set_open(1); return 0; }
    if (act == ACT_POWEROFF) { desktop_poweroff(); return 0; }
    if (act == ACT_TERMINAL) {
        int ti = 8;
        terminal_open();
        if (!g_app[ti].open) { g_app[ti].open = 1; g_app[ti].minimized = 0; g_app[ti].maximized = 0;
            wm_window_anim_begin(&g_app[ti], ANIM_WINDOW_OPEN, g_app[ti].x, g_app[ti].y, g_app[ti].w, g_app[ti].h); }
        g_app[ti].z = wm_next_z();
        wm_set_focus(ti);
        return 0;
    }
    if (act == ACT_PYTHON || act == ACT_CC || act == ACT_JAVA) {
        /* Language launchers always enter DevStudio rather than a demo runtime. */
        sysconf_set_dev_app(DEV_APP_DEVSTUDIO);
        if (!g_app[4].open) { g_app[4].open = 1; g_app[4].minimized = 0; g_app[4].maximized = 0;
            wm_window_anim_begin(&g_app[4], ANIM_WINDOW_OPEN, g_app[4].x, g_app[4].y, g_app[4].w, g_app[4].h); }
        g_app[4].z = wm_next_z();
        wm_set_focus(4);
        if (act == ACT_CC) {
            devstudio_open_language("C/C++");
        } else if (act == ACT_PYTHON) {
            devstudio_open_language("Python");
        } else {
            devstudio_open_language("Java");
        }
        return 0;
    }
    if (act >= 0 && act < NAPP) {
        app_t* a = &g_app[act];
        if (act == 4) dev_open();                // 开发: 依据偏好打开 DevStudio / VSCode
        if (act == 5) editor_open();             // 编辑器: 重新扫描磁盘文件
        if (act == 6) filemgr_open();            // 文件管理器: 扫描磁盘文件
        if (act == 7) disktool_open();           // 磁盘工具: 扫描磁盘/FS 信息
        if (!a->open) { a->open = 1; a->minimized = 0; a->maximized = 0;
            wm_window_anim_begin(a, ANIM_WINDOW_OPEN, a->x, a->y, a->w, a->h); }
        else if (a->minimized) { a->minimized = 0;
            wm_window_anim_begin(a, ANIM_WINDOW_OPEN, a->x, a->y, a->w, a->h); }
        a->z = wm_next_z();
        wm_set_focus(act);
    }
    return 0;
}

// 供其它模块 (如文件管理器) 在内部拉起一个窗口应用
void wm_launch_app(int act) { launch(act); }

// ============================================================
// 快捷键系统
// ============================================================
static void hk_toggle_start(void);
static void hk_next_win(void);
static void hk_prev_win(void);
static void hk_show_desktop(void);
static void hk_terminal(void);
static void hk_taskmgr(void);
static void hk_help(void);
static void hk_close_win(void);
static void hk_logoff(void);
static void hk_settings(void);
static void cycle_focus(int dir);

// hotkey_t 定义已迁移至 shell.h (gui_framework Phase 4)

// Ctrl+Alt+L: 从 FSOS 文件系统加载并运行 Linux ELF (默认 HELLO.ELF)
static void hk_linux_test(void) {
    int pid = linux_exec("HELLO.ELF");
    if (pid < 0) {
        // 失败: 在状态栏提示 (借用 taskmgr 思路? 这里简单忽略)
    }
}

const hotkey_t g_hotkeys[] = {
    { KB_MOD_GUI,             KEY_WIN, "Win 开始菜单",   hk_toggle_start },
    { KB_MOD_ALT,             KEY_TAB, "Alt+Tab 切换窗口",      hk_next_win },
    { KB_MOD_ALT|KB_MOD_SHIFT,KEY_TAB, "Shift+Tab 上一窗口",      hk_prev_win },
    { KB_MOD_GUI,            'd',     "Win+D 显示桌面", hk_show_desktop },
    { KB_MOD_GUI,            't',     "Win+T 终端",            hk_terminal },
    { KB_MOD_GUI,            'r',     "Win+R 运行",      hk_terminal },
    { KB_MOD_GUI,            'e',     "Win+E 设置",            hk_settings },
    { KB_MOD_GUI,            'l',     "Win+L 注销",                hk_logoff },
    { KB_MOD_CTRL|KB_MOD_ALT,'t',     "Ctrl+Alt+T 终端", hk_terminal },
    { KB_MOD_CTRL|KB_MOD_ALT,KEY_DEL, "Ctrl+Alt+Del 任务",           hk_taskmgr },
    { KB_MOD_CTRL|KB_MOD_ALT,'l',     "Ctrl+Alt+L Linux", hk_linux_test },
    { KB_MOD_ALT,            KEY_F4,  "Alt+F4 关闭窗口",        hk_close_win },
    { 0,                      KEY_F1, "F1 快捷键帮助",           hk_help },
};
#define NHK (sizeof(g_hotkeys)/sizeof(g_hotkeys[0]))
const int g_nhk = (int)(sizeof(g_hotkeys)/sizeof(g_hotkeys[0]));

static void cycle_focus(int dir) {
    int order[NAPP]; int n = 0;
    for (int i = 0; i < NAPP; i++) if (g_app[i].open) order[n++] = i;
    if (n == 0) return;
    int cur = -1;
    for (int i = 0; i < n; i++) if (order[i] == g_focus) { cur = i; break; }
    int nx = (cur + dir + n) % n;
    wm_set_focus(order[nx]);
    g_app[g_focus].z = wm_next_z();
    g_app[g_focus].minimized = 0;
}

static void hk_toggle_start(void){ g_start_open = !g_start_open; startmenu_set_open(g_start_open); g_show_help = 0; }
static void hk_next_win(void){ cycle_focus(+1); }
static void hk_prev_win(void){ cycle_focus(-1); }
static void hk_show_desktop(void){
    for (int i = 0; i < NAPP; i++) if (g_app[i].open) g_app[i].minimized = 1;
    g_start_open = 0; g_show_help = 0;
}
static void hk_terminal(void){ g_start_open = 0; startmenu_set_open(0); g_show_help = 0; launch(ACT_TERMINAL); g_wm_skip = 1; }
static void hk_taskmgr(void){ g_start_open = 0; startmenu_set_open(0); g_show_help = 0; taskmgr_run(); g_wm_skip = 1; }
static void hk_help(void){ g_show_help = !g_show_help; g_start_open = 0; startmenu_set_open(0); }
static void hk_close_win(void){
    if (g_focus >= 0 && g_focus < NAPP && g_app[g_focus].open) {
        close_app(g_focus);
    }
    wm_set_focus(-1);
    for (int i = NAPP - 1; i >= 0; i--)
        if (g_app[i].open && !g_app[i].minimized) { wm_set_focus(i); break; }
}
static void hk_logoff(void){ g_wm_quit = 1; }
// 供控制中心"切换用户"/"锁定"调用: 回到登录界面要求重新登录
void wm_request_logoff(void){ g_wm_quit = 1; }
static void hk_settings(void){ g_start_open = 0; startmenu_set_open(0); g_show_help = 0; launch(3); }

static int handle_hotkey(int k) {
    int mods = kb_mods();
    for (int i = 0; i < (int)NHK; i++) {
        if (mods == g_hotkeys[i].mods && k == g_hotkeys[i].key) {
            g_hotkeys[i].fn();
            return 1;
        }
    }
    return 0;
}

// gui_framework Phase 4: draw_help_overlay 已迁移至 shell.c
#define draw_help_overlay desktop_draw_help_overlay

// ============================================================
// gui_framework Phase 6: draw_start_menu/sm_* 已迁移至 startmenu.c
// ============================================================
#define draw_start_menu  startmenu_draw
#define sm_width         startmenu_width
#define sm_height        startmenu_height
#define sm_x0            startmenu_x0
#define sm_y0            startmenu_y0
#define sm_item_rect     startmenu_item_rect

// ============================================================
// gui_framework Phase 4: draw_icons/icon_hit 已迁移至 shell.c
// ============================================================
#define draw_icons    desktop_draw_icons
#define icon_hit      desktop_icon_hit

// ============================================================
// gui_framework Phase 5: draw_taskbar 已迁移至 taskbar.c
// ============================================================
#define draw_taskbar    taskbar_draw

// ============================================================
// gui_framework Phase 4: 壁纸/提示/右键菜单/电源 已迁移至 shell.c
// ============================================================
#define draw_wallpaper     desktop_draw_wallpaper
#define draw_tip           desktop_draw_tip
#define draw_context_menu  desktop_draw_context_menu
#define ctx_hit            desktop_ctx_hit
#define ctx_execute        desktop_ctx_execute
#define wm_poweroff        desktop_poweroff
#define wm_reboot          desktop_reboot

// ============================================================
// hires: 根据实际分辨率调整窗口初始几何 (比例缩放, 320x200 下不变)
// ============================================================
static void wm_layout_init(void) {
    int W=SCREEN_W,H=WM_TASKBAR_Y;
    for(int i=0;i<NAPP;i++){
        int w=(i==4)?(W*68/100):((i==6)?(W*54/100):((i==8)?(W*62/100):(W*44/100)));
        int h=(i==4)?(H*68/100):((i==6)?(H*50/100):((i==8)?(H*58/100):(H*44/100)));
        int maxw=W-88,maxh=H-78;if(w>maxw)w=maxw;if(h>maxh)h=maxh;
        if(W>=1200&&w<600) w=600;
        if(H>=800&&h<380) h=380;
        int x=(W-w)/2,y=(H-h)/2;
        if(i==6){ x=W/2-w/2; y=H*17/100; }
        if(i==8){ x=(W-w)/2; y=H*19/100; }
        if(i==4){ x=(W-w)/2; y=H*10/100; }
        if(x<16) x=16;
        if(y<50) y=50;
        if(x+w>W-16) x=W-16-w;
        if(y+h>H-10) y=H-10-h;
        g_app[i].x=x;g_app[i].y=y;g_app[i].w=w;g_app[i].h=h;g_app[i].ox=x;g_app[i].oy=y;g_app[i].ow=w;g_app[i].oh=h;
    }
}


static void wm_session_reset(void) {
    for (int i = 0; i < NAPP; i++) {
        if (g_app[i].open) close_app(i);
        else { g_app[i].open=0; g_app[i].minimized=0; g_app[i].maximized=0; g_app[i].z=0; }
    }
    wm_layout_init();
    wm_set_maxz(1); wm_set_focus(-1); wm_set_drag(-1);
    wm_set_drag_offset(0,0);
    g_start_open=0; startmenu_set_open(0); g_show_help=0; g_ctx_open=0; g_sel_icon=-1;
    g_last_click_icon=-1; g_wm_skip=0; g_wm_quit=0; g_force_redraw=1;
    g_tip_visible=1; g_tip_until=get_ticks()+8000;
    g_wallpaper_palette_ready=0;
    launch(6);
}
// ============================================================
// gui_framework Phase 3: 全屏绘制回调 (注册至 compositor)
// ============================================================
static int s_draw_mx = 0, s_draw_my = 0;  // 当前鼠标位置 (供绘制回调使用)

static void wm_draw_all(const rect_t* region) {
    int mx = s_draw_mx, my = s_draw_my;
    // 脏区局部重绘: 每个脏区域单独重绘, 绘制前设置裁剪框 (region==NULL 表示全屏)。
    // 各元素先判断是否与裁剪框相交, 不相交则整段跳过, 避免无谓的整幅迭代。
    if (region) gfx_set_clip(region->x, region->y, region->x + region->w - 1, region->y + region->h - 1);
    else        gfx_reset_clip();

    draw_wallpaper();

    int top_h = desktop_topbar_h();
    if (gfx_clip_intersects(0, 0, VGA_W - 1, top_h - 1))
        desktop_draw_topbar();
    if (gfx_clip_intersects(2, 66, 142, TASKBAR_Y - 1))
        draw_icons(mx, my);
    for (int zz = 0; zz <= g_maxz; zz++) {
        for (int i = 0; i < NAPP; i++) {
            app_t* a2 = &g_app[i];
            if (!a2->open || (a2->minimized && !wm_window_anim_active(a2))) continue;
            if (a2->z == zz && gfx_clip_intersects(a2->x, a2->y, a2->x + a2->w - 1, a2->y + a2->h - 1))
                draw_window(a2, (g_focus == i));
        }
    }
    if (gfx_clip_intersects(0, 0, VGA_W - 1, SCREEN_H - 1))
        draw_tip();
    if (g_start_open && gfx_clip_intersects(startmenu_x0(), startmenu_y0(),
            startmenu_x0() + startmenu_width() - 1, startmenu_y0() + startmenu_height() - 1))
        draw_start_menu(mx, my);
    if (gfx_clip_intersects(0, TASKBAR_Y, VGA_W - 1, SCREEN_H - 1))
        draw_taskbar(mx, my);
    if (sidebar_is_open() && gfx_clip_intersects(VGA_W - 300, 0, VGA_W - 1, SCREEN_H - 1))
        sidebar_draw(mx, my);
    if (g_show_help) draw_help_overlay();
    if (g_ctx_open) draw_context_menu(mx, my);
    draw_cursor(mx, my);

    if (region) gfx_reset_clip();
}

// 纯鼠标移动时, 仅失效"光标矩形 + 受鼠标影响的热区", 由合成器局部重绘 -> 帧率大幅提升。
// 结构性变化(开窗口/点击/按键/动画/右键菜单/首帧/强制)仍走全屏重绘(行为不变)。
// 对热区按"旧位置或新位置任一端在内"失效, 同时覆盖进入与离开边界, 避免悬停高亮残留。
static void wm_inval_motion(int px0, int py0, int px1, int py1) {
    int W = VGA_W, H = SCREEN_H;
    int th = desktop_topbar_h();
    // 光标矩形 (覆盖箭头 + 1px 黑描边); 尺寸随 draw_cursor 的缩放 sc
    int sc = (VGA_W >= 1400) ? 2 : 1;
    int cw = 14 * sc + 2, ch = 16 * sc + 2;
    compositor_invalidate(px0 - 1, py0 - 1, cw, ch);
    compositor_invalidate(px1 - 1, py1 - 1, cw, ch);
    // 顶栏 (含日期时间)
    if ((py0 < th) || (py1 < th)) compositor_invalidate(0, 0, W, th);
    // 任务栏/Dock (悬停高亮随鼠标)
    if ((py0 >= TASKBAR_Y) || (py1 >= TASKBAR_Y)) compositor_invalidate(0, TASKBAR_Y, W, WM_TASKBAR_H);
    // 侧边栏 (控制中心)
    if ((px0 >= W - 300 && sidebar_is_open()) || (px1 >= W - 300 && sidebar_is_open()))
        compositor_invalidate(W - 300, 0, 300, H);
    // 桌面图标列
    int io0 = (px0 >= 18 && px0 <= 142), io1 = (px1 >= 18 && px1 <= 142);
    if (io0 || io1) { int iy0 = 66, iy1 = TASKBAR_Y; if (iy1 < iy0 + 1) iy1 = iy0 + 1;
        compositor_invalidate(2, iy0, 140, iy1 - iy0); }
    // 窗口: 任一端落在某窗口内 -> 失效该窗口整窗 (标题栏按钮/客户区悬停随鼠标)
    for (int i = 0; i < NAPP; i++) {
        app_t* a = &g_app[i];
        if (!a->open || a->minimized) continue;
        int hit0 = (px0 >= a->x && px0 < a->x + a->w && py0 >= a->y && py0 < a->y + a->h);
        int hit1 = (px1 >= a->x && px1 < a->x + a->w && py1 >= a->y && py1 < a->y + a->h);
        if (hit0 || hit1) compositor_invalidate(a->x, a->y, a->w, a->h);
    }
    if (g_start_open)
        compositor_invalidate(startmenu_x0(), startmenu_y0(), startmenu_width(), startmenu_height());
    if (g_ctx_open) {
        int mw = 300, mh = 220, cx = g_ctx_x, cy = g_ctx_y;
        if (cx + mw > VGA_W) cx = VGA_W - mw;
        if (cy + mh > SCREEN_H) cy = SCREEN_H - mh;
        compositor_invalidate(cx, cy, mw, mh);
    }
}

// ============================================================
// 事件队列处理器 (T3.2/T3.3): 按优先级分发点击/按键
// 优先级: 控制中心/上下文菜单 > 开始菜单 > 任务栏 > 窗口 > 桌面 > 键盘
// ============================================================
static int g_ev_quit = 0;        // 处理器请求退出桌面
static int g_ev_skip = 0;        // 处理器请求跳过一帧
static int g_ev_key_dirty = 0;   // 本帧有按键 -> 触发重绘

// PRIO_CTX_MENU: 控制中心浮动面板 + 上下文菜单 + 右键转发
static int h_priority_top(const gui_event_t* ev) {
    if (ev->type == EV_MOUSE_RIGHT) {
        // 桌面空白处弹出上下文菜单 (开始菜单打开或任务栏上不弹)
        if (g_start_open || ev->y >= TASKBAR_Y) return 0;
        int over_win = 0;
        for (int i = 0; i < NAPP; i++) {
            app_t* a = &g_app[i];
            if (a->open && !a->minimized &&
                ev->x >= a->x && ev->x < a->x + a->w && ev->y >= a->y && ev->y < a->y + a->h) { over_win = 1; break; }
        }
        if (!over_win) {
            g_ctx_open = 1; g_ctx_x = ev->x; g_ctx_y = ev->y; g_force_redraw = 1; filemgr_close_ctx();
        } else {
            for (int i = 0; i < NAPP; i++) {
                app_t* a = &g_app[i];
                if (a->open && !a->minimized && a->on_rmouse &&
                    ev->x >= a->x && ev->x < a->x + a->w && ev->y >= a->y && ev->y < a->y + a->h) {
                    safe_callback_on_rmouse(a, ev->x, ev->y);
                    g_force_redraw = 1;
                    break;
                }
            }
        }
        return 1;
    }
    if (ev->type != EV_MOUSE_LEFT) return 0;
    // 控制中心浮动面板优先接收点击
    if (sidebar_on_click(ev->x, ev->y)) { g_force_redraw = 1; return 1; }
    // 上下文菜单打开时优先消费
    if (g_ctx_open) {
        int hit = ctx_hit(ev->x, ev->y);
        if (hit >= 0) ctx_execute(hit);
        else g_ctx_open = 0;
        g_force_redraw = 1;
        return 1;
    }
    return 0;
}

// PRIO_START_MENU
static int h_start_menu(const gui_event_t* ev) {
    if (ev->type != EV_MOUSE_LEFT) return 0;
    if (!g_start_open) return 0;
    int footer_act = startmenu_footer_hit(ev->x, ev->y);
    if (footer_act == ACT_LOGOFF) {
        g_start_open = 0; startmenu_set_open(0); g_ev_quit = 1; return 1;
    }
    if (footer_act == ACT_POWEROFF) {
        g_start_open = 0; startmenu_set_open(0); launch(ACT_POWEROFF); return 1;
    }
    int hit_item = -1;
    for (int i = 0; i < (int)NSTART; i++) {
        int rx, ry, rw, rh;
        sm_item_rect(i, &rx, &ry, &rw, &rh);
        if (ev->x >= rx && ev->x <= rx + rw && ev->y >= ry && ev->y <= ry + rh) { hit_item = i; break; }
    }
    if (hit_item >= 0) {
        int act = g_start[hit_item].act;
        g_start_open = 0; startmenu_set_open(0);
        if (launch(act)) g_ev_quit = 1;
    } else {
        int in_menu = (ev->x >= sm_x0() && ev->x <= sm_x0() + sm_width() && ev->y >= sm_y0() && ev->y <= sm_y0() + sm_height());
        if (!in_menu) { g_start_open = 0; startmenu_set_open(0); }
    }
    return 1;
}

// PRIO_TASKBAR
static int h_taskbar(const gui_event_t* ev) {
    if (ev->type != EV_MOUSE_LEFT) return 0;
    if (ev->y < TASKBAR_Y) return 0;
    int action = taskbar_hit_action(ev->x, ev->y);
    if (action == 0) { g_start_open = !g_start_open; startmenu_set_open(g_start_open); }
    else if (action == ACT_TERMINAL) { hk_terminal(); }
    else if (action == ACT_POWEROFF) { desktop_poweroff(); }
    else if (action >= 0 && action < NAPP) { launch(action); }
    return 1;
}

// PRIO_WINDOW: 标题栏拖动/关闭/最大化/最小化/客户区转发
static int h_window(const gui_event_t* ev) {
    if (ev->type != EV_MOUSE_LEFT) return 0;
    if (ev->y >= TASKBAR_Y) return 0;
    int mx = ev->x, my = ev->y;
    int hit_idx = -1, hit_what = -1, bestz = -1;
    for (int i = 0; i < NAPP; i++) {
        if (!g_app[i].open || g_app[i].minimized) continue;
        int what;
        if (win_hit(&g_app[i], mx, my, &what) && g_app[i].z > bestz) { bestz = g_app[i].z; hit_idx = i; hit_what = what; }
    }
    if (hit_idx < 0) return 0;
    wm_set_focus(hit_idx);
    g_app[hit_idx].z = wm_next_z();
    if (hit_what == 0) {                                    // 关闭
        close_app(hit_idx);
    } else if (hit_what == 1) {                             // 最大化/还原
        app_t* a = &g_app[hit_idx];
        if (a->maximized) {
            wm_window_anim_begin(a, ANIM_WINDOW_RESTORE, a->ox, a->oy, a->ow, a->oh);
            a->x = a->ox; a->y = a->oy; a->w = a->ow; a->h = a->oh; a->maximized = 0;
        } else {
            a->ox = a->x; a->oy = a->y; a->ow = a->w; a->oh = a->h;
            wm_window_anim_begin(a, ANIM_WINDOW_RESTORE, 0, 0, SCREEN_W, TASKBAR_Y);
            a->x = 0; a->y = 0; a->w = SCREEN_W; a->h = TASKBAR_Y; a->maximized = 1;
        }
    } else if (hit_what == 2) {                             // 最小化
        app_t* a = &g_app[hit_idx];
        anim_id_t aid = wm_window_anim_begin(a, ANIM_WINDOW_MINIMIZE, 10, TASKBAR_Y+10, 48, 48);
        if (aid == ANIM_INVALID) a->minimized = 1;
    } else if (hit_what == 3) {                             // 标题栏拖动
        app_t* a = &g_app[hit_idx];
        if (a->maximized) { a->x = a->ox; a->y = a->oy; a->w = a->ow; a->h = a->oh; a->maximized = 0; }
        wm_set_drag(hit_idx);
        wm_set_drag_offset(mx - a->x, my - a->y);
    } else if (hit_what == 4) {                             // 客户区: 转发鼠标给应用
        safe_callback_on_mouse(&g_app[hit_idx], mx, my, 1);
    }
    g_force_redraw = 1;
    return 1;
}

// PRIO_DESKTOP: 桌面图标 (双击打开, 终端单击直接进入)
static int h_desktop(const gui_event_t* ev) {
    if (ev->type != EV_MOUSE_LEFT) return 0;
    int hi = icon_hit(ev->x, ev->y);
    if (hi >= 0) {
        uint32_t now = get_ticks();
        int act = g_icons[hi].act;
        if (act == ACT_TERMINAL) {
            g_sel_icon = hi; g_last_click_icon = -1;
            if (launch(act)) g_ev_quit = 1; else g_ev_skip = 1;
        } else if (hi == g_last_click_icon && (now - g_last_click_t) < 400) {
            g_last_click_icon = -1;
            if (launch(act)) g_ev_quit = 1;
        } else {
            g_sel_icon = hi; g_last_click_icon = hi; g_last_click_t = now;
        }
    } else {
        g_sel_icon = -1; g_last_click_icon = -1;
    }
    g_force_redraw = 1;
    return 1;
}

// PRIO_KEYBOARD: 全局快捷键 > 焦点窗口 > 关闭/退出
static int h_keyboard(const gui_event_t* ev) {
    if (ev->type != EV_KEY) return 0;
    int k = ev->key;
    g_ev_key_dirty = 1;
    int consumed = 0;
    if (handle_hotkey(k)) {
        consumed = 1;
    } else if (g_focus >= 0 && g_focus < NAPP && g_app[g_focus].open &&
               !g_app[g_focus].minimized && g_app[g_focus].on_key) {
        consumed = safe_callback_on_key(&g_app[g_focus], k);
    }
    if (consumed == GUI_KEY_CLOSE) {
        if (g_focus >= 0 && g_focus < NAPP && g_app[g_focus].open) {
            close_app(g_focus);
            wm_set_focus(-1);
            for (int i = NAPP - 1; i >= 0; i--) {
                if (g_app[i].open && !g_app[i].minimized) { wm_set_focus(i); break; }
            }
        }
    } else if (!consumed) {
        if (k == KEY_ESC) g_ev_quit = 1;
    }
    return 1;
}

static void wm_register_handlers(void) {
    event_queue_register_handler(PRIO_CTX_MENU, h_priority_top);
    event_queue_register_handler(PRIO_START_MENU, h_start_menu);
    event_queue_register_handler(PRIO_TASKBAR, h_taskbar);
    event_queue_register_handler(PRIO_WINDOW, h_window);
    event_queue_register_handler(PRIO_DESKTOP, h_desktop);
    event_queue_register_handler(PRIO_KEYBOARD, h_keyboard);
}

// ============================================================
// 主循环
// ============================================================

// 阶段 3 回滚开关 (T8.1): 置 1 时不经事件队列模块的优先级表, 主循环内直接按
// 优先级顺序同步调用各处理器; 置 0 使用 event_queue_dispatch()。用于隔离
// “事件队列分发”引入的回归, 不改动各处理器逻辑本身。
#define LEGACY_EVENT_LOOP 0

void wm_demo_run(void) {
    (void)g_gfx_inited;

    // gui_framework Phase 2: 初始化运行时 Theme API (深色主题 + 分辨率自适应)
    theme_init(gfx_is_lfb(), gfx_font_scale());

    // gui_framework Phase 3: 初始化合成器与事件队列
    compositor_init(wm_draw_all);
    event_queue_init();
    wm_register_handlers();      // T3.2: 注册 6 档优先级事件处理器
    anim_init();
    sidebar_init();

    // 每一次“进入桌面”都是一个新的 UI session：关闭残留窗口、清空焦点、重置布局。
    // 这样注销 -> 登录 -> 再进入桌面不会叠加上一会话的窗口。
    wm_session_reset();

    mp_fsos_prefetch();

    mouse_state_t prev; mouse_get(&prev);
    int skip = 0;
    int s_first = 1;
    int s_tip   = -1;
    uint32_t s_tb = 0;

    for (;;) {
        mouse_state_t m;
        mouse_get(&m);
        if (skip) { prev = m; event_queue_sync_mouse(); skip = 0; }

        int mx = m.x, my = m.y;

        // T3.2: 采集本帧边沿事件 (左键/右键/按键) 并按优先级分发
        // 优先级: 控制中心/上下文菜单 > 开始菜单 > 任务栏 > 窗口 > 桌面 > 键盘
#if LEGACY_EVENT_LOOP
        event_queue_poll_from_drivers();
        {
            gui_event_t ev;
            while (event_queue_pop(&ev)) {
                if (h_priority_top(&ev)) continue;
                if (h_start_menu(&ev))   continue;
                if (h_taskbar(&ev))      continue;
                if (h_window(&ev))       continue;
                if (h_desktop(&ev))      continue;
                h_keyboard(&ev);
            }
        }
#else
        event_queue_poll_from_drivers();
        event_queue_dispatch();
#endif
        if (g_ev_quit) break;
        if (g_ev_skip) { g_ev_skip = 0; skip = 1; }   // 桌面图标启动后跳过一帧

        // 滚轮：发送给“光标下最顶层、且能处理按键的窗口”，而非仅焦点窗口。
        // 这样把鼠标移到终端上滚动即可滚动，不必先点击获取焦点——否则终端未获得
        // 焦点时滚轮毫无反应，表现为“有时在终端里滚动没有用”。
        if (m.wheel != 0) {
            int hit_idx = -1, bestz = -1;
            for (int i = 0; i < NAPP; i++) {
                app_t* a = &g_app[i];
                if (a->open && !a->minimized && a->on_key &&
                    mx >= a->x && mx < a->x + a->w &&
                    my >= a->y && my < a->y + a->h && a->z > bestz) {
                    bestz = a->z; hit_idx = i;
                }
            }
            if (hit_idx >= 0) {
                app_t* fa = &g_app[hit_idx];
                int wk = (m.wheel > 0) ? KEY_WHEEL_UP : KEY_WHEEL_DOWN;
                int n = m.wheel > 4 ? 4 : (m.wheel < -4 ? -4 : m.wheel);
                if (n == 0) n = (m.wheel > 0) ? 1 : -1;
                int count = n > 0 ? n : -n;
                for (int wi = 0; wi < count; ++wi) safe_callback_on_key(fa, wk);
                g_force_redraw = 1;
            }
        }

        // 右键事件已由事件队列 PRIO_CTX_MENU 处理器 (h_priority_top) 处理

        // 新手提示超时自动消失
        if (g_tip_visible && (int)(get_ticks() - g_tip_until) >= 0) g_tip_visible = 0;

        // ---- 拖动中 ----
        if (g_drag >= 0) {
            if (!m.left) {
                wm_set_drag(-1);
            } else {
                app_t* a = &g_app[g_drag];
                if (!a->maximized) {
                    a->x = mx - g_dox; a->y = my - g_doy;
                    if (a->x < 0) a->x = 0;
                    if (a->y < 2) a->y = 2;
                    if (a->y + TITLE_H > TASKBAR_Y) a->y = TASKBAR_Y - TITLE_H;
                }
            }
        }
        // 点击事件已由事件队列按优先级分发:
        //   PRIO_CTX_MENU(h_priority_top) > PRIO_START_MENU > PRIO_TASKBAR > PRIO_WINDOW > PRIO_DESKTOP

        // ---- 键盘已由事件队列 PRIO_KEYBOARD 处理器处理 (h_keyboard) ----
        // 全局快捷键 > 焦点窗口 on_key > GUI_KEY_CLOSE 关闭窗口 / ESC 退出桌面
        if (dev_take_skip()) skip = 1;
        if (editor_take_skip()) skip = 1;
        if (g_wm_skip) { skip = 1; g_wm_skip = 0; }
        if (g_wm_quit) break;

        // ---- 脏帧判定 + 局部重绘: 结构性变化全屏重绘, 纯鼠标移动仅失效热区 ----
        int anim_active = anim_tick();
        wm_window_anim_tick();
        int btn_changed = (m.left != prev.left) || (m.right != prev.right);
        int moved       = (m.x != prev.x) || (m.y != prev.y);
        int tip_changed = (g_tip_visible != s_tip);
        int k_dirty     = g_ev_key_dirty; g_ev_key_dirty = 0;
        int on_tick_dirty = (g_focus >= 0 && g_focus < NAPP && g_app[g_focus].open
                             && !g_app[g_focus].minimized && g_app[g_focus].on_tick
                             && safe_callback_on_tick(&g_app[g_focus]));
        s_tip   = g_tip_visible;
        s_first = 0;
        if (g_force_redraw) g_force_redraw = 0;
        rtc_update();                       // 空闲帧也推进 RTC, 否则任务栏时钟不走
        int rtc_dirty = 0;
        if (rtc_ok) {
            uint32_t tb = (uint32_t)(rtc_h * 3600 + rtc_m * 60);
            if (g_app[2].open) tb = tb * 60 + (uint32_t)rtc_s;   // 时钟窗口打开时按秒刷新
            if (tb != s_tb) { s_tb = tb; rtc_dirty = 1; }
        } else {
            uint32_t tb = get_ticks() / 1000u;
            if (tb != s_tb) { s_tb = tb; rtc_dirty = 1; }
        }

        int dirty = s_first || anim_active || moved || btn_changed || k_dirty
                 || tip_changed || on_tick_dirty || g_ctx_open || g_force_redraw || rtc_dirty;

        if (dirty) {
            // gui_framework Phase 4: 结构化变化全屏重绘; 纯鼠标移动/时钟秒进仅局部重绘
            s_draw_mx = mx; s_draw_my = my;
            int structural = s_first || anim_active || btn_changed || k_dirty
                          || tip_changed || on_tick_dirty || g_ctx_open || g_force_redraw;
            if (structural) {
                compositor_invalidate_all();
            } else if (moved) {
                wm_inval_motion(prev.x, prev.y, m.x, m.y);
            } else if (rtc_dirty) {
                int th = desktop_topbar_h();          // 顶栏含时钟
                compositor_invalidate(0, 0, VGA_W, th);
                if (g_app[2].open) compositor_invalidate(g_app[2].x, g_app[2].y, g_app[2].w, g_app[2].h);
            }
            compositor_compose();
            compositor_flip();
        }
        prev = m;
        __asm__ volatile("hlt");
    }

    gfx_clear_idx(COL_WALL_F);
    gfx_flip();
    extern void vga_init(void);
    vga_init();
}
