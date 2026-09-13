// wm.c - FSOS（自由安全操作系统）桌面环境（Aurora：macOS 主导的轻量桌面）
//
// 特性:
//   - 320x200 mode13h 双缓冲, 经 GOP 整数放大到 960x600 上屏
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
#include "aurora_wallpaper.h"
#include "linux.h"     // Linuxulator: Ctrl+Alt+L 运行 FS 中的 Linux ELF (HELLO.ELF)
// text2x() 时钟大数字从内核内置字体取字形 (与 vga/gfx/cjk 一致, UEFI 下 0xB0000 不可靠)
#include "boot/uefi/font8x8.h"

// 桌面用动态分辨率 (vga.h 的 VGA_W/SCREEN_H), 适配 mode13h 320x200 或 VBE 高分辨率
#define SCREEN_W   VGA_W
#define TASKBAR_H  (20 * THEME_SF)   // 底部 Dock 保留高度 (随分辨率缩放)
#define TASKBAR_Y  (SCREEN_H - TASKBAR_H)
#define TITLE_H    (18 * THEME_SF)   // 标题栏高 (随分辨率缩放)

#define MAXAPP     8

// 特殊启动动作
#define ACT_TERMINAL  (-2)
#define ACT_LOGOFF    (-3)
#define ACT_PYTHON    (-4)
#define ACT_CC        (-5)
#define ACT_JAVA      (-6)

typedef struct {
    const char* name;
    int        x, y, w, h;
    int        open;
    int        minimized;
    int        maximized;  // 最大化状态
    int        ox, oy, ow, oh; // 最大化前原几何
    int        z;
    void (*draw)(int x, int y, int w, int h);
    // 焦点窗口接收键盘事件; 返回 1 表示已消费 (不再走全局快捷键, 例如 ESC)
    int  (*on_key)(int k);
    // 鼠标事件: 点击落到本窗口客户区时由 wm 转发 (x,y 屏幕坐标, ldown=1 按下边沿)
    int  (*on_mouse)(int x, int y, int ldown);
    // 周期性回调: 返回 1 表示本窗口需要重绘 (用于光标闪烁等)
    int  (*on_tick)(void);
    // 右键按下边沿: 落到本窗口客户区时由 wm 转发 (x,y 屏幕坐标); 用于应用内右键菜单
    int  (*on_rmouse)(int mx, int my);
} app_t;

static void d_about(int x, int y, int w, int h);
static void d_users(int x, int y, int w, int h);
static void d_clock(int x, int y, int w, int h);
static void d_settings(int x, int y, int w, int h);

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
    return 0;
}
static int dev_tick(void) {
    if (sysconf_dev_app() == DEV_APP_VSCODE) return editor_tick();
    return 0;
}

static app_t g_app[MAXAPP] = {
    { "关于",     8,   8, 204, 112, 1,0,0,  8,  8,204,112, 1, d_about,  0 },
    { "用户",   126,  16, 184, 140, 0,0,0, 126,16,184,140, 0, d_users,  0 },
    { "时钟",   146,  56, 148, 108, 0,0,0, 146,56,148,108, 0, d_clock,  0 },
    { "设置",    44,   8, 196, 152, 0,0,0,  44, 8,196,152, 0, d_settings, 0 },
    // 开发: 统一编辑 Python / C-C++ / Java 工程文件 (可在 DevStudio / VSCode 间切换)
    { "开发",     6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, dev_draw, dev_key, dev_on_mouse, dev_tick, 0 },
    // FSOS 原生 VSCode 风格代码编辑器 (多标签/侧边栏/语法高亮/命令面板/鼠标交互)
    { "编辑器",    6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, editor_draw, editor_key, editor_on_mouse, editor_tick, 0 },
    // 文件管理器: 浏览/打开/新建/删除/改名 + 磁盘信息
    { "文件管理器", 6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, filemgr_draw, filemgr_key, filemgr_on_mouse, 0, filemgr_on_rmouse },
    // 磁盘工具: 磁盘几何 / 逻辑分区 / 格式化文件系统
    { "磁盘工具",   6,   6, 308, 170, 0,0,0,  6, 6,308,170, 0, disktool_draw, disktool_key, disktool_on_mouse, 0, 0 },
};
#define NAPP 8

static int   g_maxz = 1;
static int   g_focus = 0;
static int   g_drag = -1;
static int   g_dox, g_doy;
static int   g_gfx_inited = 0;
static int   g_wallpaper_palette_ready = 0;

static int   g_start_open = 0;
static int   g_sel_icon = -1;
static uint32_t g_last_click_t = 0;
static int   g_last_click_icon = -1;
static int   g_show_help = 0;
static int   g_wm_skip = 0;
static int   g_wm_quit = 0;

// 右键上下文菜单
static int   g_ctx_open = 0;
static int   g_ctx_x = 0, g_ctx_y = 0;
static int   g_force_redraw = 0;
static const struct { const char* label; int act; } g_ctx[] = {
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

// 新手提示: 进入桌面后显示 8 秒
static int   g_tip_visible = 1;
static uint32_t g_tip_until = 0;

// 开始菜单项 (9 项, 3x3 网格)
static const struct { const char* label; int act; int kind; } g_start[] = {
    { "关于",    0,           0 },
    { "用户",    1,           1 },
    { "时钟",    2,           2 },
    { "设置",    3,           3 },
    { "终端",    ACT_TERMINAL, 4 },
    { "Python",  ACT_PYTHON,   5 },
    { "C/C++",   ACT_CC,       6 },
    { "Java",    ACT_JAVA,     7 },
    { "开发",    4,            9 },
    { "编辑器",  5,            9 },
    { "文件管理器", 6,         10 },
    { "磁盘工具", 7,           11 },
    { "注销",    ACT_LOGOFF,   8 },
};
#define NSTART (sizeof(g_start)/sizeof(g_start[0]))

// 桌面图标 (2 列 x 4 行)
static const struct { const char* label; int act; int kind; } g_icons[] = {
    { "关于",    0,           0 },
    { "用户",    1,           1 },
    { "时钟",    2,           2 },
    { "终端",    ACT_TERMINAL, 4 },
    { "Python",  ACT_PYTHON,   5 },
    { "C/C++",   ACT_CC,       6 },
    { "Java",    ACT_JAVA,     7 },
    { "开发",    4,           9 },
    { "设置",    3,           3 },
    { "文件管理器", 6,        10 },
    { "磁盘工具", 7,           11 },
};
#define NICON (sizeof(g_icons)/sizeof(g_icons[0]))

// ============================================================
// 基础绘制助手
// ============================================================
static void fill(int x0,int y0,int x1,int y1,uint8_t c){ gfx_fill_idx(x0,y0,x1,y1,c); }
static void rect(int x0,int y0,int x1,int y1,uint8_t c){ gfx_rect_idx(x0,y0,x1,y1,c); }
static void txt(int x,int y,const char* s,uint8_t f,uint8_t b){ cjk_text(x,y,s,f,b); }
static void fill_round(int x0,int y0,int x1,int y1,int r,uint8_t c){
    if (gfx_is_lfb()) {
        uint8_t cr,cg,cb; gfx_idx_rgb(c,&cr,&cg,&cb);
        gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,cr,cg,cb);
    } else gfx_fill_round_idx(x0,y0,x1,y1,r,c);
}
static void rect_round(int x0,int y0,int x1,int y1,int r,uint8_t c){
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
    // 白色箭头主体: 第 r 行自尖端向右展宽, 宽 1..6
    for (int r = 0; r < 9; r++) {
        int w = r + 1; if (w > 6) w = 6;
        for (int i = 0; i <= w; i++) gfx_pixel_idx(x + i + 1, y + r + 1, COL_BLACK);   // 黑描边(右下 1px)
        for (int i = 0; i < w; i++) gfx_pixel_idx(x + i, y + r, COL_WHITE);            // 白身
    }
    // 尾柄: 从箭头右缘斜向后拉出的下尾, 增强指针辨识
    for (int r = 6; r < 13; r++) {
        int cx = x + (r - 6) + 4;
        gfx_pixel_idx(cx + 1, y + r + 1, COL_BLACK);
        gfx_pixel_idx(cx, y + r, COL_WHITE);
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

static void draw_icon(int kind, int x, int y) {
    // 图标外框 26x20：扁平圆角卡片，适合 Launchpad 与桌面共用。
    uint8_t bg = COL_ACCENT;
    switch (kind) {
        case 0: bg = COL_ACCENT_SOFT; break;   // 关于
        case 1: bg = COL_TASK_HI;      break;   // 用户
        case 2: bg = COL_PANEL_HI;     break;   // 时钟
        case 3: bg = COL_PANEL;        break;   // 设置
        case 4: bg = COL_BLACK;        break;   // 终端
        case 5: bg = COL_ACCENT;       break;   // Python
        case 6: bg = COL_ORANGE;       break;   // C/C++
        case 7: bg = COL_BROWN;        break;   // Java
        case 8: bg = COL_LRED;         break;   // 注销
        case 9: bg = COL_LGREEN;       break;   // 开发
        case 10: bg = COL_YELLOW;      break;   // 文件管理器
        case 11: bg = COL_DGRAY;       break;   // 磁盘工具
    }
    fill_round(x, y, x + 25, y + 19, RADIUS_CTRL, bg);
    rect_round(x, y, x + 25, y + 19, RADIUS_CTRL, COL_WHITE);
    int cx = x + 13, cy = y + 10;

    if (kind == 0) {
        // 关于: 信息符号 "i" (白)
        fill(cx - 1, cy - 4, cx + 1, cy + 1, COL_WHITE);
        fcircle(cx, cy + 5, 2, COL_WHITE);
    }
    else if (kind == 1) {
        // 用户: 人头 + 肩 (白)
        fcircle(cx, cy - 2, 3, COL_WHITE);
        fill(cx - 5, cy + 2, cx + 5, cy + 8, COL_WHITE);
        fcircle(cx - 5, cy + 5, 3, COL_WHITE);
        fcircle(cx + 5, cy + 5, 3, COL_WHITE);
    }
    else if (kind == 2) {
        // 时钟: 白表盘 + 指针
        fring(cx, cy, 7, 2, COL_WHITE);
        fill(cx, cy - 4, cx, cy + 1, COL_WHITE);       // 分针
        fill(cx - 3, cy, cx + 0, cy + 0, COL_WHITE);   // 时针
    }
    else if (kind == 3) {
        // 设置: 三条滑杆
        for (int i = 0; i < 3; i++) {
            int yy = y + 5 + i * 5;
            fill(x + 4, yy, x + 22, yy, COL_WHITE);
            int knob = x + 7 + i * 5;
            fill(knob - 1, yy - 1, knob + 1, yy + 1, COL_WHITE);
        }
    }
    else if (kind == 4) {
        // 终端: 黑底白框, 绿色 >_ 提示符
        rect_square(x + 4, y + 3, 18, 14, COL_WHITE);
        // ">" 形 (绿)
        gfx_pixel_idx(x + 7,  y + 7, COL_LGREEN);
        gfx_pixel_idx(x + 8,  y + 8, COL_LGREEN);
        gfx_pixel_idx(x + 9,  y + 9, COL_LGREEN);
        gfx_pixel_idx(x + 10, y + 10, COL_LGREEN);
        // "_" 光标
        fill(x + 11, y + 12, x + 17, y + 13, COL_LGREEN);
    }
    else if (kind == 5) {
        // Python: 蓝底, 三个黄色 >>>
        for (int row = 0; row < 3; row++) {
            int yy = y + 5 + row * 5;
            gfx_pixel_idx(x + 6,  yy, COL_YELLOW);
            gfx_pixel_idx(x + 7,  yy + 1, COL_YELLOW);
            gfx_pixel_idx(x + 8,  yy + 2, COL_YELLOW);
            gfx_pixel_idx(x + 9,  yy + 1, COL_YELLOW);
        }
    }
    else if (kind == 6) {
        // C/C++: 橙底白字 "C++" 放大 2x 放不下; 改用花括号 { }
        fill(cx - 4, cy - 4, cx - 3, cy + 4, COL_WHITE); // {
        fill(cx - 3, cy - 4, cx - 1, cy - 3, COL_WHITE);
        fill(cx - 3, cy + 3, cx - 1, cy + 4, COL_WHITE);
        fill(cx + 3, cy - 4, cx + 4, cy + 4, COL_WHITE); // }
        fill(cx + 1, cy - 4, cx + 2, cy - 3, COL_WHITE);
        fill(cx + 1, cy + 3, cx + 2, cy + 4, COL_WHITE);
    }
    else if (kind == 7) {
        // Java: 棕底咖啡杯
        fill(cx - 4, cy - 2, cx + 4, cy + 6, COL_WHITE);       // 杯身
        fill(cx + 5, cy - 1, cx + 7, cy + 3, COL_WHITE);         // 把手
        // 蒸汽
        gfx_pixel_idx(cx - 2, cy - 5, COL_LGRAY);
        gfx_pixel_idx(cx + 0, cy - 6, COL_LGRAY);
        gfx_pixel_idx(cx + 2, cy - 5, COL_LGRAY);
    }
    else if (kind == 8) {
        // 注销: 红底电源符号 (圆环缺口 + 竖线)
        fring(cx, cy + 1, 6, 2, COL_WHITE);  // 注意顶部缺口自然存在(圆环上半部分在画布内)
        fill(cx - 1, cy - 5, cx + 1, cy + 2, COL_WHITE); // 竖线
    }
    else if (kind == 10) {
        // 文件管理器: 黄底白色文件夹
        fill(x + 5, y + 7, x + 21, y + 16, COL_WHITE);  // 文件夹主体
        fill(x + 5, y + 7, x + 9, y + 10, bg);          // 顶部缺口(还原底色)
        fill(x + 6, y + 5, x + 12, y + 7, COL_WHITE);   // 标签页
    }
    else if (kind == 11) {
        // 磁盘工具: 灰底白色硬盘 (盘体 + 中心孔)
        fill(x + 4, y + 5, x + 22, y + 15, COL_WHITE);  // 盘体
        fcircle(cx, cy, 3, bg);                          // 中心孔(还原底色)
        fill(cx - 1, cy - 1, cx + 1, cy + 1, COL_BLACK); // 孔心
    }
}

// ============================================================
// 应用窗口内容
// ============================================================
static void d_about(int x, int y, int w, int h) {
    (void)w; (void)h;
    txt(x + 6, y + 6,  "FSOS（自由安全操作系统）", COL_ACCENT, COL_WHITE);
    txt(x + 6, y + 22, "Aurora 轻量中文桌面", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 38, "双击图标打开窗口", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 54, "底部 Dock 打开应用程序", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 70, "ESC 退出桌面  F1 快捷键", COL_LGRAY, COL_WHITE);
}

static void d_users(int x, int y, int w, int h) {
    (void)w; (void)h;
    int cnt = user_count();
    txt(x + 6, y + 4, "账户列表:", COL_SHADOW, COL_WHITE);
    for (int i = 0; i < cnt && i < 6; i++) {
        const UserRec* u = user_get(i);
        if (!u) break;
        char line[32];
        const char* role = "普通";
        if (u->role == ROLE_ADMIN) role = "管理员";
        if (u->role == ROLE_ROOT)  role = "root";
        // 名字为 ASCII, 直接拼接
        int p = 0;
        line[p++] = (char)('0' + (i % 10));
        line[p++] = ' ';
        for (int c = 0; u->name[c] && p < 24; c++) line[p++] = u->name[c];
        line[p++] = ' ';
        for (int c = 0; role[c] && p < 30; c++) line[p++] = role[c];
        line[p] = 0;
        txt(x + 6, y + 20 + i * 16, line,
            (u->role >= ROLE_ADMIN) ? COL_ACCENT : COL_SHADOW, COL_WHITE);
    }
}

static void d_clock(int x, int y, int w, int h) {
    (void)w; (void)h;
    extern int rtc_h, rtc_m, rtc_s, rtc_ok;
    extern int rtc_y, rtc_mo, rtc_d;
    char buf[16];

    // 大数字时间
    if (rtc_ok) {
        buf[0] = (char)('0' + (rtc_h / 10)); buf[1] = (char)('0' + (rtc_h % 10));
        buf[2] = ':';
        buf[3] = (char)('0' + (rtc_m / 10)); buf[4] = (char)('0' + (rtc_m % 10));
        buf[5] = ':';
        buf[6] = (char)('0' + (rtc_s / 10)); buf[7] = (char)('0' + (rtc_s % 10));
        buf[8] = 0;
        text2x(x + 8, y + 8, buf, COL_SHADOW, COL_WHITE);

        // 日期
        int p = 0;
        buf[p++] = '2'; buf[p++] = '0'; buf[p++] = (char)('0' + (rtc_y / 10)); buf[p++] = (char)('0' + (rtc_y % 10));
        buf[p++] = '-'; buf[p++] = (char)('0' + (rtc_mo / 10)); buf[p++] = (char)('0' + (rtc_mo % 10));
        buf[p++] = '-'; buf[p++] = (char)('0' + (rtc_d / 10)); buf[p++] = (char)('0' + (rtc_d % 10));
        buf[p] = 0;
        txt(x + 8, y + 44, buf, COL_SHADOW, COL_WHITE);
    } else {
        txt(x + 8, y + 20, "实时时钟不可用", COL_LRED, COL_WHITE);
    }

    uint32_t ms = get_ticks();
    int s = ms / 1000;
    int mm = s / 60, ss = s % 60;
    buf[0] = (char)('0' + (mm / 10)); buf[1] = (char)('0' + (mm % 10));
    buf[2] = ':';
    buf[3] = (char)('0' + (ss / 10)); buf[4] = (char)('0' + (ss % 10));
    buf[5] = 0;
    txt(x + 8, y + 62, "运行 ", COL_LGRAY, COL_WHITE);
    txt(x + 38, y + 62, buf, COL_LGREEN, COL_WHITE);
}

static void d_settings(int x, int y, int w, int h) {
    (void)w; (void)h;
    txt(x + 6, y + 6,  "系统设置", COL_ACCENT, COL_WHITE);
    txt(x + 6, y + 22, "当前版本: FSOS 演示版", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 38, "主题: Aurora (夜色)", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 54, "后续可在此调整", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 70, "壁纸、语言、日期等", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 90, "按 Win+T 或点击终端图标", COL_SHADOW, COL_WHITE);
    txt(x + 6, y + 106, "可运行 Python 脚本", COL_SHADOW, COL_WHITE);
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

static void rtc_update(void) {
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
// ============================================================
// 按钮区域 (从左到右): 最小化 最大化 关闭
static int win_btn_x(app_t* a, int btn) {
    // btn: 0=close, 1=max, 2=min
    (void)a;
    // macOS 风格的左上三色控制点。逻辑编号保持不变，避免影响现有鼠标行为。
    return a->x + 6 + btn * 15;
}

static void draw_window(app_t* a, int focused) {
    int x = a->x, y = a->y, w = a->w, h = a->h;
    uint8_t border = focused ? COL_ACCENT : COL_SHADOW;
    uint8_t tbg    = focused ? COL_UI_TITLE_SOFT : COL_PANEL;

    // 窗口阴影分层 (modern_ui: 聚焦 2px 强, 非聚焦 1px 弱, 最大化无阴影)
    if (!a->maximized) {
        int off = focused ? SHADOW_OFF : 1;
        fill(x + off, y + h, x + w + off - 1, y + h + off - 1, SHADOW_COL);
        fill(x + w, y + off, x + w + off - 1, y + h + off - 1, SHADOW_COL);
    }

    // 客户区背景 (ui_polish: 圆角外框, 最大化退化直角)
    int rw = a->maximized ? 0 : RADIUS_WINDOW;
    fill_round(x, y, x + w - 1, y + h - 1, rw, COL_WHITE);
    rect_round(x, y, x + w - 1, y + h - 1, rw, border);

    // 标题栏 (ui_polish: 圆角 RADIUS_CTRL)
    fill_round(x + 1, y + 1, x + w - 2, y + TITLE_H - 2, RADIUS_CTRL, tbg);
    txt(x + 54, y + 1, a->name, focused ? COL_WHITE : COL_SHADOW, tbg);

    // 按钮 (ui_polish: 圆角 RADIUS_CTRL, 可点击区域尺寸不变)
    for (int b = 2; b >= 0; b--) {
        int bx = win_btn_x(a, b);
        int by = y + 3;
        uint8_t dot = (b == 0) ? COL_LRED : (b == 1 ? COL_YELLOW : COL_LGREEN);
        fcircle(bx + 5, by + 5, 5, dot);
        if (b == 2) {
            // 最小化: 底横线
            fill(bx + 3, by + 6, bx + 8, by + 6, COL_SHADOW);
        } else if (b == 1) {
            // 最大化/还原: 小方框
            int sq = a->maximized ? 1 : 0;
            if (sq) {
                // 还原符号: 两个重叠方块
                fill(bx + 3, by + 3, bx + 6, by + 6, COL_SHADOW);
                rect(bx + 5, by + 5, bx + 9, by + 9, COL_SHADOW);
            } else {
                fill(bx + 3, by + 3, bx + 8, by + 8, COL_SHADOW);
                rect(bx + 3, by + 3, bx + 8, by + 8, dot); // 内部镂空
            }
        } else {
            // 关闭 X
            for (int i = 2; i <= 8; i++) {
                gfx_pixel_idx(bx + i, by + i, COL_SHADOW);
                gfx_pixel_idx(bx + i, by + 10 - i, COL_SHADOW);
            }
        }
    }

    // 客户区内容
    if (a->draw) a->draw(x + 2, y + TITLE_H + 1, w - 4, h - TITLE_H - 3);
}

// 命中窗口子区域. *what: 0=close 1=max/restore 2=min 3=title 4=client
//
// ui_polish: 圆角命中判定 —— 点在矩形包围盒外返回 0; 点在四角外侧空白
// 三角区返回 0; 否则返回 1。半径钳制与 gfx_fill_round_idx 一致, 保证命中与绘制几何相同。
static int round_rect_hit(int mx, int my, int x, int y, int w, int h, int r) {
    if (mx < x || my < y || mx >= x + w || my >= y + h) return 0;
    int m = w < h ? w : h;
    int cap = m / 4;
    if (r > cap) r = cap;
    if (r <= 0) return 1;
    int xr = x + r, yb = y + r;          // 左上角内圆心
    if (mx < xr && my < yb) {
        int dx = xr - mx, dy = yb - my;
        if (dx * dx + dy * dy > r * r) return 0;
    }
    int xrb = x + w - r, y1_ = y + r;    // 右上角内圆心
    if (mx >= xrb && my < y1_) {
        int dx = mx - xrb + 1, dy = y1_ - my;
        if (dx * dx + dy * dy > r * r) return 0;
    }
    int x2_ = x + r, ybb = y + h - r;    // 左下角内圆心
    if (mx < x2_ && my >= ybb) {
        int dx = x2_ - mx, dy = my - ybb + 1;
        if (dx * dx + dy * dy > r * r) return 0;
    }
    int xrb2 = x + w - r, ybb2 = y + h - r; // 右下角内圆心
    if (mx >= xrb2 && my >= ybb2) {
        int dx = mx - xrb2 + 1, dy = my - ybb2 + 1;
        if (dx * dx + dy * dy > r * r) return 0;
    }
    return 1;
}

static int win_hit(app_t* a, int mx, int my, int* what) {
    int x = a->x, y = a->y, w = a->w, h = a->h;
    if (mx < x || mx >= x + w || my < y || my >= y + h) { *what = -1; return 0; }
    int r = a->maximized ? 0 : RADIUS_WINDOW;
    if (!round_rect_hit(mx, my, x, y, w, h, r)) { *what = -1; return 0; }
    if (my >= y + 3 && my <= y + 13) {
        int bx = win_btn_x(a, 0);
        if (mx >= bx && mx <= bx + 11) { *what = 0; return 1; }
        bx = win_btn_x(a, 1);
        if (mx >= bx && mx <= bx + 11) { *what = 1; return 1; }
        bx = win_btn_x(a, 2);
        if (mx >= bx && mx <= bx + 11) { *what = 2; return 1; }
    }
    if (my < y + TITLE_H) { *what = 3; return 1; }
    *what = 4; return 1;
}

// ============================================================
// 启动动作
// ============================================================
static int launch(int act) {
    if (act == ACT_LOGOFF) return 1;
    if (act == ACT_TERMINAL) { terminal_run(); return 0; }
    if (act == ACT_PYTHON) { lang_launch("Python", 0, "python-repl"); return 0; }
    if (act == ACT_CC)     { lang_launch("C/C++",  0, "c-demo");     return 0; }
    if (act == ACT_JAVA)   { lang_launch("Java",   0, "java-demo");  return 0; }
    if (act >= 0 && act < NAPP) {
        app_t* a = &g_app[act];
        if (act == 4) dev_open();                // 开发: 依据偏好打开 DevStudio / VSCode
        if (act == 5) editor_open();             // 编辑器: 重新扫描磁盘文件
        if (act == 6) filemgr_open();            // 文件管理器: 扫描磁盘文件
        if (act == 7) disktool_open();           // 磁盘工具: 扫描磁盘/FS 信息
        if (!a->open) { a->open = 1; a->minimized = 0; a->maximized = 0; }
        else if (a->minimized) { a->minimized = 0; }
        a->z = ++g_maxz;
        g_focus = act;
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

typedef struct {
    int          mods;
    int          key;
    const char*  label;
    void       (*fn)(void);
} hotkey_t;

// Ctrl+Alt+L: 从 FSOS 文件系统加载并运行 Linux ELF (默认 HELLO.ELF)
static void hk_linux_test(void) {
    int pid = linux_exec("HELLO.ELF");
    if (pid < 0) {
        // 失败: 在状态栏提示 (借用 taskmgr 思路? 这里简单忽略)
    }
}

static const hotkey_t g_hotkeys[] = {
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

static void cycle_focus(int dir) {
    int order[NAPP]; int n = 0;
    for (int i = 0; i < NAPP; i++) if (g_app[i].open) order[n++] = i;
    if (n == 0) return;
    int cur = -1;
    for (int i = 0; i < n; i++) if (order[i] == g_focus) { cur = i; break; }
    int nx = (cur + dir + n) % n;
    g_focus = order[nx];
    g_app[g_focus].z = ++g_maxz;
    g_app[g_focus].minimized = 0;
}

static void hk_toggle_start(void){ g_start_open = !g_start_open; g_show_help = 0; }
static void hk_next_win(void){ cycle_focus(+1); }
static void hk_prev_win(void){ cycle_focus(-1); }
static void hk_show_desktop(void){
    for (int i = 0; i < NAPP; i++) if (g_app[i].open) g_app[i].minimized = 1;
    g_start_open = 0; g_show_help = 0;
}
static void hk_terminal(void){ g_start_open = 0; g_show_help = 0; terminal_run(); g_wm_skip = 1; }
static void hk_taskmgr(void){ g_start_open = 0; g_show_help = 0; taskmgr_run(); g_wm_skip = 1; }
static void hk_help(void){ g_show_help = !g_show_help; g_start_open = 0; }
static void hk_close_win(void){
    for (int i = 0; i < NAPP; i++)
        if (i == g_focus && g_app[i].open) {
            g_app[i].open = 0; g_app[i].minimized = 0; g_app[i].maximized = 0; g_app[i].z = 0;
        }
    g_focus = -1;
    for (int i = NAPP - 1; i >= 0; i--)
        if (g_app[i].open && !g_app[i].minimized) { g_focus = i; break; }
}
static void hk_logoff(void){ g_wm_quit = 1; }
static void hk_settings(void){ g_start_open = 0; g_show_help = 0; launch(3); }

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

// F1 帮助覆盖层
static void draw_help_overlay(void) {
    int x0 = 4, y0 = 4, w = 312, h = 184;
    int colw = 152;
    fill_round(x0, y0, x0 + w - 1, y0 + h - 1, RADIUS_PANEL, COL_TITLEBG);
    rect_round(x0, y0, x0 + w - 1, y0 + h - 1, RADIUS_PANEL, COL_ACCENT);
    txt(x0 + 8, y0 + 4,  "快捷键", COL_YELLOW, COL_TITLEBG);
    txt(x0 + 8, y0 + 22, "Aurora 桌面快捷操作", COL_LGRAY, COL_TITLEBG);
    int rows = ((int)NHK + 1) / 2;
    int y = y0 + 42;
    for (int i = 0; i < (int)NHK; i++) {
        int col = i / rows;          // 0=左列, 1=右列
        int r   = i % rows;
        cjk_text(x0 + 8 + col * colw, y + r * 16, g_hotkeys[i].label,
                 COL_WHITE, COL_TITLEBG);
    }
    cjk_text(x0 + 8, y0 + h - 18, "再按 F1 关闭", COL_LGREEN, COL_TITLEBG);
}

// ============================================================
// 应用程序面板（Launchpad 式网格）
// ============================================================
#define SM_TILE_W   62
#define SM_TILE_H   40
#define SM_COLS     5
#define SM_TOP_H    18

// 行数按菜单项数自动推导 (新增应用无需改这里)
#define SM_ROWS     (((int)NSTART + SM_COLS - 1) / SM_COLS)

static int sm_width(void) { return SM_COLS * SM_TILE_W + 4; }
static int sm_height(void) { return SM_TOP_H + SM_ROWS * SM_TILE_H; }
static int sm_x0(void) { return (SCREEN_W - sm_width()) / 2; }
static int sm_y0(void) { return TASKBAR_Y - (SM_TOP_H + SM_ROWS * SM_TILE_H); }

static void sm_item_rect(int i, int* rx, int* ry, int* rw, int* rh) {
    int c = i % SM_COLS;
    int r = i / SM_COLS;
    *rx = sm_x0() + 2 + c * SM_TILE_W;
    *ry = sm_y0() + SM_TOP_H + r * SM_TILE_H;
    *rw = SM_TILE_W - 1;
    *rh = SM_TILE_H - 1;
}

static void draw_start_menu(int mx, int my) {
    int x0 = sm_x0(), y0 = sm_y0();
    int w = sm_width(), h = sm_height();

    // 深色面板 + 系统色标题条。使用固定网格，不要求用户学习搜索或分类操作。
    fill_round(x0, y0, x0 + w - 1, y0 + h - 1, RADIUS_PANEL, COL_TASKBAR);
    rect_round(x0, y0, x0 + w - 1, y0 + h - 1, RADIUS_PANEL, COL_SHADOW);
    fill_round(x0, y0, x0 + w - 1 - RADIUS_CTRL, y0 + SM_TOP_H - 1, RADIUS_CTRL, COL_ACCENT);
    txt(x0 + 8, y0 + 1, "应用程序", COL_WHITE, COL_ACCENT);
    txt(x0 + w - 38, y0 + 1, "FSOS", COL_WHITE, COL_ACCENT);

    for (int i = 0; i < (int)NSTART; i++) {
        int rx, ry, rw, rh;
        sm_item_rect(i, &rx, &ry, &rw, &rh);
        int hover = (mx >= rx && mx <= rx + rw && my >= ry && my <= ry + rh);
        if (hover) fill_round(rx, ry, rx + rw, ry + rh, RADIUS_CTRL, COL_UI_DOCK_HI_SOFT);
        // 图标居中
        int ix = rx + (rw - 26) / 2;
        int iy = ry + 2;
        draw_icon(g_start[i].kind, ix, iy);
        // 标签居中 (超宽则省略)
        int lw = cjk_text_w(g_start[i].label);
        if (lw > rw - 2) lw = rw - 2;
        int lx = rx + (rw - lw) / 2;
        cjk_text_ellipsis(lx, ry + 22, g_start[i].label, lw,
                          hover ? COL_ACCENT : COL_SHADOW, hover ? COL_ACCENT_SOFT : COL_TASKBAR);
    }
}

// ============================================================
// 桌面图标 (2 列 x 4 行)
// ============================================================
#define ICON_COLS   3
#define ICON_X0     16
#define ICON_COLW   100
#define ICON_TOP    12
#define ICON_STEP   42

static int icon_x(int i) { return ICON_X0 + (i % ICON_COLS) * ICON_COLW; }
static int icon_y(int i) { return ICON_TOP + (i / ICON_COLS) * ICON_STEP; }

static void draw_icons(int mx, int my) {
    for (int i = 0; i < (int)NICON; i++) {
        int x = icon_x(i);
        int y = icon_y(i);
        int hover = (mx >= x - 2 && mx <= x + 31 && my >= y - 2 && my <= y + 39);
        int selected = (i == g_sel_icon);

        // 选中/悬停高亮背景块 (ui_polish: 圆角 RADIUS_CTRL)
        uint8_t hilite = selected ? COL_ACCENT_SOFT : (hover ? COL_UI_DOCK_HI_SOFT : COL_WALL_C);
        fill_round(x - 2, y - 2, x + 31, y + 39, RADIUS_CTRL, hilite);

        // 图标
        draw_icon(g_icons[i].kind, x + 2, y);

        // 标签 (居中, 超宽则省略)
        int lw = cjk_text_w(g_icons[i].label);
        if (lw > ICON_COLW - 4) lw = ICON_COLW - 4;
        int lx = x + 15 - lw / 2;
        if (lx < 0) lx = 0;
        if (lx + lw > SCREEN_W) lx = SCREEN_W - lw;
        cjk_text_ellipsis(lx, y + 22, g_icons[i].label, lw, COL_WHITE, hilite);
    }
}

static int icon_hit(int mx, int my) {
    for (int i = 0; i < (int)NICON; i++) {
        int x = icon_x(i);
        int y = icon_y(i);
        if (mx >= x - 2 && mx <= x + 31 && my >= y - 2 && my <= y + 39)
            return i;
    }
    return -1;
}

// ============================================================
// 悬浮 Dock
// ============================================================
static void draw_taskbar(int mx, int my) {
    int y = TASKBAR_Y;
    // 不再铺满屏幕底部；保留壁纸，绘制可辨识的悬浮 Dock。
    fill_round(2, y + 1, SCREEN_W - 3, SCREEN_H - 2, RADIUS_PANEL, COL_TASKBAR);
    rect_round(2, y + 1, SCREEN_W - 3, SCREEN_H - 2, RADIUS_PANEL, COL_SHADOW);

    // 应用启动器：九点网格，兼顾 macOS Launchpad 与 Windows 的开始入口。
    int sbx = 6, sbw = 32;
    int sbhover = (my >= y + 2 && my <= SCREEN_H - 2 && mx >= sbx && mx <= sbx + sbw);
    uint8_t sbbg = (g_start_open || sbhover) ? COL_UI_DOCK_HI_SOFT : COL_TASKBAR;
    fill_round(sbx, y + 2, sbx + sbw, SCREEN_H - 2, RADIUS_CTRL, sbbg);
    rect_round(sbx, y + 2, sbx + sbw, SCREEN_H - 2, RADIUS_CTRL, COL_TASK_HI);
    int ix = sbx + 11, iy = y + 5;
    for (int gy = 0; gy < 3; gy++)
        for (int gx = 0; gx < 3; gx++)
            fcircle(ix + gx * 5, iy + gy * 4, 1,
                    (g_start_open || sbhover) ? COL_WHITE : COL_TASK_HI);

    // 已开窗口按钮
    int bx = sbx + sbw + 6;
    for (int i = 0; i < NAPP; i++) {
        app_t* a = &g_app[i];
        if (!a->open) continue;
        int bw = cjk_text_w(a->name) + 14;
        int bhover = (my >= y + 2 && my <= SCREEN_H - 2 && mx >= bx && mx <= bx + bw);
        uint8_t bg = (g_focus == i && !a->minimized) ? COL_ACCENT :
                     (bhover ? COL_UI_DOCK_HI_SOFT : COL_TASKBAR);
        fill_round(bx, y + 2, bx + bw, SCREEN_H - 2, RADIUS_CTRL, bg);
        rect_round(bx, y + 2, bx + bw, SCREEN_H - 2, RADIUS_CTRL, COL_TASK_HI);
        txt(bx + 6, y + 2, a->name, (bg == COL_ACCENT) ? COL_WHITE : COL_SHADOW, bg);
        // Dock 的小圆点表示运行中；焦点窗口使用系统蓝，其他窗口使用中性灰。
        fcircle(bx + bw / 2, SCREEN_H - 4, 1,
                (g_focus == i && !a->minimized) ? COL_ACCENT : COL_TASK_HI);
        bx += bw + 4;
        if (bx > 250) break; // 防止覆盖时钟区
    }

    // 时钟 (RTC HH:MM)
    rtc_update();
    char clk[8];
    if (rtc_ok) {
        clk[0] = (char)('0' + (rtc_h / 10)); clk[1] = (char)('0' + (rtc_h % 10));
        clk[2] = ':';
        clk[3] = (char)('0' + (rtc_m / 10)); clk[4] = (char)('0' + (rtc_m % 10));
        clk[5] = 0;
    } else {
        clk[0] = '-'; clk[1] = '-'; clk[2] = ':'; clk[3] = '-'; clk[4] = '-'; clk[5] = 0;
    }
    int cw = cjk_text_w(clk);
    int cx = SCREEN_W - 4 - cw;
    fill_round(cx - 3, y + 2, cx + cw + 2, SCREEN_H - 2, RADIUS_CTRL, COL_TASKBAR);
    rect_round(cx - 3, y + 2, cx + cw + 2, SCREEN_H - 2, RADIUS_CTRL, COL_TASK_HI);
    txt(cx, y + 2, clk, COL_WHITE, COL_TASKBAR);
}

// ============================================================
// 壁纸渐变
// ============================================================
static void draw_wallpaper(void) {
    int h = TASKBAR_Y;
    if (!g_wallpaper_palette_ready && !gfx_is_lfb()) {
        for (int i = 0; i < AURORA_WALLPAPER_COLORS; i++) {
            const uint8_t* p = &aurora_wallpaper_palette[i * 3];
            gfx_set_palette_rgb((uint8_t)(32 + i), p[0], p[1], p[2]);
        }
        // modern_ui: COL_UI_* 柔和扩展色 DAC 注入 (8bpp 下与 gfx_palette_ext 语义一致;
        // gfx_set_palette_rgb 接收 8bit 后内部 >>2 还原 6bit DAC)
        static const uint8_t ui_ext[6][3] = {
            { 0x16 << 2, 0x28 << 2, 0x18 << 2 },   // 160 SOFT_OK
            { 0x2A << 2, 0x1A << 2, 0x1A << 2 },   // 161 SOFT_ERR
            { 0x2B << 2, 0x25 << 2, 0x18 << 2 },   // 162 SOFT_WARN
            { 0x10 << 2, 0x1C << 2, 0x2F << 2 },   // 163 BG_SOFT
            { 0x17 << 2, 0x23 << 2, 0x3B << 2 },   // 164 TITLE_SOFT
            { 0x20 << 2, 0x2F << 2, 0x3F << 2 },   // 165 DOCK_HI_SOFT
        };
        for (int i = 0; i < 6; i++) {
            gfx_set_palette_rgb((uint8_t)(COL_UI_BASE + i),
                                ui_ext[i][0], ui_ext[i][1], ui_ext[i][2]);
        }
        g_wallpaper_palette_ready = 1;
    }
    // 位图原始比例正好是 mode13h 的 320x200；VBE 时按屏幕缩放。
    for (int yy = 0; yy < h; yy++) {
        int sy = yy * AURORA_WALLPAPER_HEIGHT / h;
        for (int xx = 0; xx < SCREEN_W; xx++) {
            int sx = xx * AURORA_WALLPAPER_W / SCREEN_W;
            uint8_t ci = aurora_wallpaper_pixels[sy * AURORA_WALLPAPER_W + sx];
            if (gfx_is_lfb()) {
                const uint8_t* p = &aurora_wallpaper_palette[ci * 3];
                gfx_pixel_rgb(xx, yy, p[0], p[1], p[2]);
            } else {
                gfx_pixel_idx(xx, yy, (uint8_t)(32 + ci));
            }
        }
    }
}

// 新手提示
static void draw_tip(void) {
    if (!g_tip_visible) return;
    int y = TASKBAR_Y - 24 * THEME_SF;
    int tw = SCREEN_W * 300 / 320;      // hires: 按屏幕宽度缩放提示框
    fill_round(4, y, tw, y + 19 * THEME_SF, RADIUS_PANEL, COL_TITLEBG);
    rect_round(4, y, tw, y + 19 * THEME_SF, RADIUS_PANEL, COL_ACCENT);
    cjk_text_ellipsis(8, y + 1, "双击图标打开窗口 · 左下角开始按钮启动更多应用",
                      tw - 12, COL_WHITE, COL_TITLEBG);
}

// ============================================================
// 右键上下文菜单
// ============================================================
static void wm_poweroff(void) {
    for (volatile int i = 0; i < 3000000; i++) {}
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    for (;;) { __asm__ volatile("cli; hlt"); }
}
static void wm_reboot(void) {
    for (volatile int i = 0; i < 3000000; i++) {}
    outb(0x64, 0xFE);
    for (;;) { __asm__ volatile("cli; hlt"); }
}
static int ctx_hit(int mx, int my) {
    int w = 132, h = NCTX * 18 + 4;
    int x = g_ctx_x, y = g_ctx_y;
    if (x + w > SCREEN_W) x = SCREEN_W - w;
    if (y + h > SCREEN_H) y = SCREEN_H - h;
    if (mx < x || mx > x + w || my < y || my > y + h) return -1;
    int i = (my - (y + 2)) / 18;
    if (i < 0 || i >= (int)NCTX) return -1;
    return i;
}
static void ctx_execute(int idx) {
    int act = g_ctx[idx].act;
    g_ctx_open = 0;
    g_force_redraw = 1;
    if (act == -99) return;                  // 刷新桌面
    if (act == -98) { wm_poweroff(); return; }
    if (act == -97) { wm_reboot();   return; }
    launch(act);                             // 终端 / 编辑器 / 关于
}
static void draw_context_menu(int mx, int my) {
    int w = 132, h = NCTX * 18 + 4;
    int x = g_ctx_x, y = g_ctx_y;
    if (x + w > SCREEN_W) x = SCREEN_W - w;
    if (y + h > SCREEN_H) y = SCREEN_H - h;
    fill_round(x, y, x + w, y + h, RADIUS_PANEL, COL_LGRAY);
    rect_round(x + 1, y + 1, x + w - 1, y + h - 1, RADIUS_PANEL, COL_DGRAY);
    for (int i = 0; i < (int)NCTX; i++) {
        int iy = y + 2 + i * 18;
        int hover = (mx >= x && mx <= x + w && my >= iy && my <= iy + 17);
        if (hover) {
            fill_round(x + 2, iy, x + w - 2, iy + 17, RADIUS_CTRL, COL_BLUE);
            cjk_text(x + 6, iy + 1, g_ctx[i].label, COL_WHITE, COL_BLUE);
        } else {
            cjk_text(x + 6, iy + 1, g_ctx[i].label, COL_BLACK, COL_LGRAY);
        }
    }
}

// ============================================================
// hires: 根据实际分辨率调整窗口初始几何 (比例缩放, 320x200 下不变)
// ============================================================
static void wm_layout_init(void) {
    static const int base[8][4] = {
        {  8,  8, 204, 112},  // 关于
        {126, 16, 184, 140},  // 用户
        {146, 56, 148, 108},  // 时钟
        { 44,  8, 196, 152},  // 设置
        {  6,  6, 308, 170},  // 开发
        {  6,  6, 308, 170},  // 编辑器
        {  6,  6, 308, 170},  // 文件管理器
        {  6,  6, 308, 170},  // 磁盘工具
    };
    for (int i = 0; i < NAPP; i++) {
        int x = base[i][0] * SCREEN_W / 320;
        int y = base[i][1] * SCREEN_H / 200;
        int w = base[i][2] * SCREEN_W / 320;
        int h = base[i][3] * SCREEN_H / 200;
        g_app[i].x = x;  g_app[i].y = y;  g_app[i].w = w;  g_app[i].h = h;
        g_app[i].ox = x; g_app[i].oy = y; g_app[i].ow = w; g_app[i].oh = h;
    }
}

// ============================================================
// 主循环
// ============================================================
void wm_demo_run(void) {
    (void)g_gfx_inited;

    // hires: 按实际分辨率调整窗口初始几何
    wm_layout_init();

    // 复位桌面状态
    for (int i = 0; i < NAPP; i++) {
        g_app[i].z = (i == 0) ? 1 : 0;
        g_app[i].maximized = 0;
    }
    g_maxz = 1; g_focus = 0; g_drag = -1;
    g_start_open = 0; g_sel_icon = -1; g_last_click_icon = -1;
    g_show_help = 0; g_wm_skip = 0; g_wm_quit = 0; g_wallpaper_palette_ready = 0;
    g_tip_visible = 1;
    g_tip_until = get_ticks() + 8000;

    mp_fsos_prefetch();

    mouse_state_t prev; mouse_get(&prev);
    int skip = 0;

    for (;;) {
        mouse_state_t m;
        mouse_get(&m);
        if (skip) { prev = m; skip = 0; }

        int ldown = m.left && !prev.left;
        int rdown = m.right && !prev.right;
        int mx = m.x, my = m.y;

        // 右键: 桌面空白处弹出上下文菜单 (窗口/任务栏/开始菜单上不弹)
        if (rdown && !g_start_open && my < TASKBAR_Y) {
            int over_win = 0;
            for (int i = 0; i < NAPP; i++) {
                app_t* a = &g_app[i];
                if (a->open && !a->minimized &&
                    mx >= a->x && mx < a->x + a->w && my >= a->y && my < a->y + a->h) { over_win = 1; break; }
            }
            if (!over_win) { g_ctx_open = 1; g_ctx_x = mx; g_ctx_y = my; g_force_redraw = 1; filemgr_close_ctx(); }
        }
        // 右键落到窗口: 转发给应用 (文件管理器等可弹出应用内右键菜单)
        if (rdown) {
            for (int i = 0; i < NAPP; i++) {
                app_t* a = &g_app[i];
                if (a->open && !a->minimized && a->on_rmouse &&
                    mx >= a->x && mx < a->x + a->w && my >= a->y && my < a->y + a->h) {
                    a->on_rmouse(mx, my);
                    g_force_redraw = 1;
                    break;
                }
            }
        }

        // 新手提示超时自动消失
        if (g_tip_visible && (int)(get_ticks() - g_tip_until) >= 0) g_tip_visible = 0;

        // ---- 拖动中 ----
        if (g_drag >= 0) {
            if (!m.left) {
                g_drag = -1;
            } else {
                app_t* a = &g_app[g_drag];
                if (!a->maximized) {
                    a->x = mx - g_dox; a->y = my - g_doy;
                    if (a->x < 0) a->x = 0;
                    if (a->y < 2) a->y = 2;
                    if (a->y + TITLE_H > TASKBAR_Y) a->y = TASKBAR_Y - TITLE_H;
                }
            }
        } else if (ldown) {
            int quit = 0;

            // 右键菜单打开时, 本点击优先消费 (命中项则执行, 否则关闭菜单)
            if (g_ctx_open) {
                int hit = ctx_hit(mx, my);
                if (hit >= 0) ctx_execute(hit);
                else g_ctx_open = 0;
                prev = m;
                continue;
            }

            // 1) 开始菜单优先
            if (g_start_open) {
                int hit_item = -1;
                for (int i = 0; i < (int)NSTART; i++) {
                    int rx, ry, rw, rh;
                    sm_item_rect(i, &rx, &ry, &rw, &rh);
                    if (mx >= rx && mx <= rx + rw && my >= ry && my <= ry + rh) { hit_item = i; break; }
                }
                if (hit_item >= 0) {
                    int act = g_start[hit_item].act;
                    g_start_open = 0;
                    if (launch(act)) { quit = 1; }
                    else if (act == ACT_TERMINAL) skip = 1;
                } else {
                    int in_menu = (mx >= sm_x0() && mx <= sm_x0() + sm_width() && my >= sm_y0() && my <= sm_y0() + sm_height());
                    if (!in_menu) g_start_open = 0;
                }
            } else {
                // 2) 任务栏
                int on_start = (mx >= 6 && mx <= 6 + 32 && my >= TASKBAR_Y + 2 && my <= SCREEN_H - 2);
                if (on_start) {
                    g_start_open = 1;
                } else if (my >= TASKBAR_Y) {
                    int bx = 6 + 32 + 6;
                    for (int i = 0; i < NAPP; i++) {
                        app_t* a = &g_app[i];
                        if (!a->open) continue;
                        int bw = cjk_text_w(a->name) + 14;
                        if (mx >= bx && mx <= bx + bw) {
                            if (a->minimized) { a->minimized = 0; a->z = ++g_maxz; g_focus = i; }
                            else if (g_focus == i && !a->minimized) { a->minimized = 1; }
                            else { a->z = ++g_maxz; g_focus = i; }
                            break;
                        }
                        bx += bw + 4;
                    }
                } else {
                    // 3) 窗口
                    int hit_idx = -1, hit_what = -1, bestz = -1;
                    for (int i = 0; i < NAPP; i++) {
                        if (!g_app[i].open || g_app[i].minimized) continue;
                        int what;
                        if (win_hit(&g_app[i], mx, my, &what) && g_app[i].z > bestz) {
                            bestz = g_app[i].z; hit_idx = i; hit_what = what;
                        }
                    }
                    if (hit_idx >= 0) {
                        g_focus = hit_idx;
                        g_app[hit_idx].z = ++g_maxz;
                        if (hit_what == 0) {            // 关闭
                            g_app[hit_idx].open = 0; g_app[hit_idx].minimized = 0; g_app[hit_idx].maximized = 0;
                        } else if (hit_what == 1) {     // 最大化/还原
                            app_t* a = &g_app[hit_idx];
                            if (a->maximized) {
                                a->x = a->ox; a->y = a->oy; a->w = a->ow; a->h = a->oh;
                                a->maximized = 0;
                            } else {
                                a->ox = a->x; a->oy = a->y; a->ow = a->w; a->oh = a->h;
                                a->x = 2; a->y = 2; a->w = 316; a->h = 176;
                                a->maximized = 1;
                            }
                        } else if (hit_what == 2) {     // 最小化
                            g_app[hit_idx].minimized = 1;
                        } else if (hit_what == 3) {     // 标题栏拖动
                            app_t* a = &g_app[hit_idx];
                            if (a->maximized) {
                                // 双击标题栏还原并跟随鼠标比例定位
                                a->x = a->ox; a->y = a->oy; a->w = a->ow; a->h = a->oh;
                                a->maximized = 0;
                            }
                            g_drag = hit_idx;
                            g_dox = mx - a->x;
                            g_doy = my - a->y;
                        } else if (hit_what == 4) {     // 客户区: 转发鼠标给应用
                            if (g_app[hit_idx].on_mouse) g_app[hit_idx].on_mouse(mx, my, 1);
                        }
                    } else {
                        // 4) 桌面图标
                        int hi = icon_hit(mx, my);
                        if (hi >= 0) {
                            uint32_t now = get_ticks();
                            if (hi == g_last_click_icon && (now - g_last_click_t) < 400) {
                                int act = g_icons[hi].act;
                                g_last_click_icon = -1;
                                if (launch(act)) quit = 1;
                                else if (act == ACT_TERMINAL) skip = 1;
                            } else {
                                g_sel_icon = hi;
                                g_last_click_icon = hi;
                                g_last_click_t = now;
                            }
                        } else {
                            g_sel_icon = -1;
                        }
                    }
                }
            }
            if (quit) break;
        }

        // ---- 键盘 ----
        // 焦点窗口 (如"开发") 优先接收按键: 它消费掉后不再走全局快捷键,
        // 因此在编辑器里按 ESC 是"返回文件列表", 而不是退出桌面。
        int k = kb_poll();
        if (k) {
            int consumed = 0;
            if (g_focus >= 0 && g_focus < NAPP && g_app[g_focus].open &&
                !g_app[g_focus].minimized && g_app[g_focus].on_key) {
                consumed = g_app[g_focus].on_key(k);
            }
            if (!consumed) {
                if (handle_hotkey(k)) {
                } else if (k == KEY_ESC) {
                    break;
                }
            }
        }
        if (dev_take_skip()) skip = 1;
        if (editor_take_skip()) skip = 1;
        if (g_wm_skip) { skip = 1; g_wm_skip = 0; }
        if (g_wm_quit) break;

        // ---- 脏帧判定: 内容无变化则整帧跳绘 (空闲零绘制, 消除每 1ms 全量重绘造成的整机卡顿) ----
        static int      s_first = 1;
        static int      s_tip   = -1;
        static uint32_t s_tb    = 0;
        int dirty = s_first
                 || (m.x != prev.x) || (m.y != prev.y) || (m.left != prev.left) || (m.right != prev.right)
                 || (k != 0)
                 || (g_tip_visible != s_tip)
                 || (g_focus >= 0 && g_focus < NAPP && g_app[g_focus].open
                     && !g_app[g_focus].minimized && g_app[g_focus].on_tick
                     && g_app[g_focus].on_tick())
                 || g_ctx_open
                 || g_force_redraw;
        s_tip   = g_tip_visible;
        s_first = 0;
        if (g_force_redraw) g_force_redraw = 0;
        rtc_update();                       // 空闲帧也推进 RTC, 否则任务栏时钟不走
        if (rtc_ok) {
            uint32_t tb = (uint32_t)(rtc_h * 3600 + rtc_m * 60);
            if (g_app[2].open) tb = tb * 60 + (uint32_t)rtc_s;   // 时钟窗口打开时按秒刷新
            if (tb != s_tb) { s_tb = tb; dirty = 1; }
        } else {
            uint32_t tb = get_ticks() / 1000u;
            if (tb != s_tb) { s_tb = tb; dirty = 1; }
        }

        if (dirty) {
            draw_wallpaper();
            draw_icons(mx, my);
            for (int zz = 0; zz <= g_maxz; zz++) {
                for (int i = 0; i < NAPP; i++) {
                    app_t* a = &g_app[i];
                    if (!a->open || a->minimized) continue;
                    if (a->z == zz) draw_window(a, (g_focus == i));
                }
            }
            draw_tip();
            if (g_start_open) draw_start_menu(mx, my);
            draw_taskbar(mx, my);
            if (g_show_help) draw_help_overlay();
            if (g_ctx_open) draw_context_menu(mx, my);
            draw_cursor(mx, my);

            gfx_flip();
        }
        prev = m;
        __asm__ volatile("hlt");
    }

    gfx_clear_idx(COL_WALL_F);
    gfx_flip();
    extern void vga_init(void);
    vga_init();
}
