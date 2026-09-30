// sidebar.c - FSOS 控制中心 (macOS 风格右上角浮动面板)
//
// 概念图结构 (自上而下):
//   用户卡: 头像 + 名称 + 在线指示 + 时间 + 日期(含星期)
//           + 三枚动作按钮 (切换用户 / 锁定 / 电源)
//   系统信息: 内存使用 / 磁盘使用 (标签 + 真实数值 + 进度条) / 网络连接
//   快捷操作: Wi-Fi / 蓝牙 / 夜间模式 开关
//   收起面板
//
// 说明:
//   - 绘制与命中测试共用 cc_measure() 推导的几何, 避免"看得见点不到"。
//   - 面板高度由内容决定并钳制在可用高度内 (宽松档 / 高度不足时的紧凑档)。
//   - 磁盘统计走 ATA, 仅在初始化/展开时刷新一次, 不在每帧读盘。
#include "sidebar.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "theme.h"
#include "theme_api.h"
#include "taskbar.h"
#include "shell.h"
#include "animation.h"
#include "compositor.h"   // 全屏脏标记 (主题切换)
#include "kheap.h"
#include "pmm.h"
#include "filesys.h"
#include "layout.h"
#include "user.h"
#include "wm.h"
#include <stdint.h>

// ==================== 状态 ====================
static int g_target = 0, g_open = 0;
static int g_wifi = 1, g_bt = 0, g_night = 0;
static anim_id_t g_anim = ANIM_INVALID;
static uint32_t g_disk_used_kb = 0, g_disk_total_kb = 1;   // 磁盘统计缓存

// ==================== 绘制助手 ====================
static void txt(int x, int y, const char* s, uint8_t f, uint8_t b) { cjk_ui_text(x, y, s, f, b); }
static void fill_round(int x0, int y0, int x1, int y1, int r, uint8_t c) {
    uint8_t R, G, B; gfx_idx_rgb(c, &R, &G, &B);
    if (gfx_is_lfb()) gfx_fill_round_rgb_aa(x0, y0, x1, y1, r, R, G, B);
    else gfx_fill_round_idx(x0, y0, x1, y1, r, c);
}
static void rect_round(int x0, int y0, int x1, int y1, int r, uint8_t c) {
    uint8_t R, G, B; gfx_idx_rgb(c, &R, &G, &B);
    if (gfx_is_lfb()) gfx_round_rect_rgb_aa(x0, y0, x1, y1, r, R, G, B);
    else gfx_round_rect_idx(x0, y0, x1, y1, r, c);
}
static void frgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) { gfx_fill_rgb(x0, y0, x1, y1, r, g, b); }
static void lrgb(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) { gfx_line_aa(x0, y0, x1, y1, r, g, b); }
static void drgb(int cx, int cy, int rr, uint8_t r, uint8_t g, uint8_t b) { gfx_disc_aa(cx, cy, rr, r, g, b); }
static void rgb2(uint8_t idx, uint8_t* r, uint8_t* g, uint8_t* b) { gfx_idx_rgb(idx, r, g, b); }

// ==================== 数值格式化 ====================
static char* put_u(char* p, uint32_t v) {
    if (v == 0) { *p++ = '0'; return p; }
    char t[12]; int i = 0;
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) *p++ = t[--i];
    return p;
}
static char* put_s(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
// 字节数 -> "X.XGB" / "X.XMB" / "XKB" / "XB"
static void fmt_bytes(char* out, uint64_t bytes) {
    char* p = out;
    if (bytes >= (1ull << 30)) {
        uint32_t d = (uint32_t)((bytes * 10ull) >> 30);
        p = put_u(p, d / 10); *p++ = '.'; p = put_u(p, d % 10); p = put_s(p, "GB");
    } else if (bytes >= (1ull << 20)) {
        uint32_t d = (uint32_t)((bytes * 10ull) >> 20);
        p = put_u(p, d / 10); *p++ = '.'; p = put_u(p, d % 10); p = put_s(p, "MB");
    } else if (bytes >= (1ull << 10)) {
        p = put_u(p, (uint32_t)(bytes >> 10)); p = put_s(p, "KB");
    } else {
        p = put_u(p, (uint32_t)bytes); p = put_s(p, "B");
    }
    *p = 0;
}
static void fmt_pair(char* out, uint64_t used, uint64_t total) {
    char a[24], b[24];
    fmt_bytes(a, used); fmt_bytes(b, total);
    char* p = out; p = put_s(p, a); p = put_s(p, " / "); p = put_s(p, b); *p = 0;
}

// ==================== 矢量图标 ====================
// 人形 (切换用户)
static void ico_person(int cx, int cy, uint8_t c) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    drgb(cx, cy - 5, 4, r, g, b);
    frgb(cx - 6, cy + 1, cx + 6, cy + 8, r, g, b);
}
// 挂锁 (锁定)
static void ico_lock(int cx, int cy, uint8_t c, uint8_t bg) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    uint8_t br, bgc, bb; rgb2(bg, &br, &bgc, &bb);
    lrgb(cx - 4, cy - 2, cx - 4, cy - 6, r, g, b);
    lrgb(cx + 4, cy - 2, cx + 4, cy - 6, r, g, b);
    lrgb(cx - 4, cy - 6, cx + 4, cy - 6, r, g, b);
    fill_round(cx - 7, cy - 1, cx + 7, cy + 10, 2, c);
    frgb(cx - 1, cy + 2, cx + 1, cy + 6, br, bgc, bb);
}
// 电源符号: 开口圆环 + 竖直笔
static void ico_power(int cx, int cy, uint8_t c, uint8_t bg) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    uint8_t br, bgc, bb; rgb2(bg, &br, &bgc, &bb);
    drgb(cx, cy + 1, 8, r, g, b);
    drgb(cx, cy + 1, 6, br, bgc, bb);
    frgb(cx - 7, cy - 9, cx + 7, cy - 4, br, bgc, bb);
    frgb(cx - 1, cy - 9, cx + 1, cy + 1, r, g, b);
}
// 内存芯片
static void ico_chip(int cx, int cy, uint8_t c, uint8_t bg) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    uint8_t br, bgc, bb; rgb2(bg, &br, &bgc, &bb);
    fill_round(cx - 7, cy - 7, cx + 7, cy + 7, 2, c);
    frgb(cx - 4, cy - 4, cx + 4, cy + 4, br, bgc, bb);
    frgb(cx - 2, cy - 10, cx - 2, cy - 8, r, g, b); frgb(cx + 2, cy - 10, cx + 2, cy - 8, r, g, b);
    frgb(cx - 2, cy + 8, cx - 2, cy + 10, r, g, b); frgb(cx + 2, cy + 8, cx + 2, cy + 10, r, g, b);
    frgb(cx - 10, cy - 2, cx - 8, cy - 2, r, g, b); frgb(cx - 10, cy + 2, cx - 8, cy + 2, r, g, b);
    frgb(cx + 8, cy - 2, cx + 10, cy - 2, r, g, b); frgb(cx + 8, cy + 2, cx + 10, cy + 2, r, g, b);
}
// 磁盘
static void ico_disk(int cx, int cy, uint8_t c, uint8_t bg) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    uint8_t br, bgc, bb; rgb2(bg, &br, &bgc, &bb);
    fill_round(cx - 8, cy - 7, cx + 8, cy + 7, 3, c);
    frgb(cx - 6, cy - 5, cx + 6, cy - 3, br, bgc, bb);
    drgb(cx, cy + 2, 2, br, bgc, bb);
}
// Wi-Fi 弧线
static void ico_wifi(int cx, int cy, int s, uint8_t c) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    lrgb(cx - s, cy - s / 2, cx, cy + s / 2, r, g, b);
    lrgb(cx, cy + s / 2, cx + s, cy - s / 2, r, g, b);
    lrgb(cx - s / 2, cy, cx, cy + s / 2, r, g, b);
    lrgb(cx, cy + s / 2, cx + s / 2, cy, r, g, b);
    frgb(cx - 1, cy + s / 2, cx + 1, cy + s / 2 + 1, r, g, b);
}
// 蓝牙符文
static void ico_bt(int cx, int cy, uint8_t c) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    lrgb(cx, cy - 7, cx, cy + 7, r, g, b);
    lrgb(cx, cy - 7, cx + 5, cy - 2, r, g, b);
    lrgb(cx + 5, cy - 2, cx - 3, cy + 3, r, g, b);
    lrgb(cx - 3, cy - 3, cx + 5, cy + 2, r, g, b);
    lrgb(cx + 5, cy + 2, cx, cy + 7, r, g, b);
}
// 月牙 (夜间模式)
static void ico_moon(int cx, int cy, uint8_t c, uint8_t bg) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    uint8_t br, bgc, bb; rgb2(bg, &br, &bgc, &bb);
    drgb(cx, cy, 7, r, g, b);
    drgb(cx + 4, cy - 2, 6, br, bgc, bb);
}
// 折叠箭头: dir=+1 向下, -1 向上
static void ico_chev(int cx, int cy, int dir, uint8_t c) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    lrgb(cx - 4, cy - 2 * dir, cx, cy + 2 * dir, r, g, b);
    lrgb(cx, cy + 2 * dir, cx + 4, cy - 2 * dir, r, g, b);
}
// 右向箭头 (区块标题 "更多")
static void ico_chev_right(int cx, int cy, uint8_t c) {
    uint8_t r, g, b; rgb2(c, &r, &g, &b);
    lrgb(cx - 2, cy - 4, cx + 2, cy, r, g, b);
    lrgb(cx + 2, cy, cx - 2, cy + 4, r, g, b);
}

// 开关 (46x26)
static void toggle(int x, int y, int on) {
    fill_round(x, y, x + 46, y + 26, 13, on ? theme_get_color_idx(COLOR_ACCENT) : theme_get_color_idx(COLOR_HOVER));
    fill_round(on ? x + 24 : x + 3, y + 4, on ? x + 42 : x + 21, y + 22, 9,
               on ? theme_get_color_idx(COLOR_FG_TITLE) : theme_get_color_idx(COLOR_FG_SOFT));
}
// 进度条 (高 9)
static void bar(int x, int y, int w, int pct, uint8_t track, uint8_t fill) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    fill_round(x, y, x + w, y + 9, 4, track);
    if (pct > 0) {
        int fw = w * pct / 100;          // 直接按比例填充, 不设最小可视宽度 (占比与数值一致)
        if (fw < 1) fw = 1;
        fill_round(x, y, x + fw, y + 9, 4, fill);
    }
}

// ==================== 几何 (绘制/命中单一来源) ====================
typedef struct {
    int x, y, w, h;
    int pad, gap;
    int top, top_h, urow_h, act_h;   // 用户卡 (含动作行)
    int sys, sys_h;                  // 系统信息卡
    int qk, qk_h;                    // 快捷操作卡
    int foot, foot_h;                // 收起面板
} cc_t;

static int pw(void) { int w = (VGA_W >= 1500 ? 360 : (VGA_W >= 900 ? 330 : 300)); if (VGA_W < 500) w = VGA_W - 20; return w; }
static int px(void) { return VGA_W - pw() - 18; }
static int inner_w(const cc_t* L) { return L->w - L->pad * 2 - 16; }
static int act_bw(const cc_t* L) { return (inner_w(L) - 16) / 3; }   // 3 列, 列间距 8

static void cc_measure(cc_t* L, int y) {
    int avail = WM_TASKBAR_Y - 62;
    int pad = theme_get_padding(PADDING_PANEL), gap = theme_get_padding(PADDING_MD), top_h = 168, urow_h = 104, sys_h = 152, qk_h = 134, foot_h = 42;
    int total = pad * 2 + top_h + sys_h + qk_h + foot_h + gap * 3;
    if (total > avail) {   // 紧凑档: 适配 600~720p 高度
        pad = 10; gap = 6; top_h = 140; urow_h = 84; sys_h = 140; qk_h = 118; foot_h = 34;
    }
    L->x = px(); L->y = y; L->w = pw();
    L->pad = pad; L->gap = gap;
    int cy = y + pad;
    L->top = cy;  L->top_h = top_h;  cy += top_h + gap;
    L->sys = cy;  L->sys_h = sys_h;  cy += sys_h + gap;
    L->qk = cy;   L->qk_h = qk_h;    cy += qk_h + gap;
    L->foot = cy; L->foot_h = foot_h; cy += foot_h + pad;
    L->h = cy - y;
    if (L->h > avail) L->h = avail;
    L->urow_h = urow_h;
    L->act_h = L->top_h - L->urow_h - 6;
}

static int act_col_x(const cc_t* L, int i) { return L->x + L->pad + 8 + i * (act_bw(L) + 8); }
static int act_row_y(const cc_t* L) { return L->top + L->urow_h + 6; }
static int qk_toggle_x(const cc_t* L) { return L->x + L->w - L->pad - 8 - 46; }
static int qk_row_y(const cc_t* L, int i) {
    int rh = (L->qk_h - 44) / 3;
    return L->qk + 34 + i * (rh + 4);
}
static int sys_rh2(const cc_t* L) { return (L->sys_h - 68) / 2; }

// ==================== 生命周期 ====================
static void refresh_disk(void) {
    int ue = 0, us = 0;
    fs_stats_total(&ue, &us);
    g_disk_used_kb = (uint32_t)(us / 2);
    g_disk_total_kb = (uint32_t)(((uint64_t)FS_DATA_SECS * 512ull) / 1024ull);
    if (g_disk_total_kb == 0) g_disk_total_kb = 1;
}

void sidebar_init(void) {
    g_target = g_open = 1;   // 默认展开 (首次进入桌面即呈现控制面板)
    g_anim = ANIM_INVALID;
    g_wifi = 1; g_bt = 0;
    g_night = (theme_get_mode() == THEME_MODE_DARK) ? 1 : 0;   // 与系统主题同步
    refresh_disk();
}
void sidebar_set_open(int open) {
    g_target = open ? 1 : 0;
    if (open) refresh_disk();
    if (g_anim != ANIM_INVALID) anim_cancel(g_anim);
    g_anim = anim_create(open ? ANIM_WINDOW_OPEN : ANIM_WINDOW_CLOSE, theme_get_anim_duration(ANIM_FADE_T));
}
void sidebar_toggle(void) { sidebar_set_open(!g_target); }
int sidebar_is_open(void) { return g_target || g_anim != ANIM_INVALID; }

void sidebar_toggle_rect(int* x, int* y, int* w, int* h) {
    // 热区 = 顶栏右侧状态簇 (wifi/音量/电池/日期时间), macOS 控制中心交互习惯
    *x = desktop_topbar_cluster_x() - 22; *y = 0;
    *w = VGA_W - *x - 10; *h = desktop_topbar_h();
}

static int prog(void) {
    if (g_anim == ANIM_INVALID) return g_target ? 1000 : 0;
    if (!anim_is_active(g_anim)) { g_open = g_target; g_anim = ANIM_INVALID; return g_open ? 1000 : 0; }
    return anim_get_progress(g_anim);
}

// ==================== 绘制 ====================
static void card(int x, int y, int w, int h) {
    int r = theme_get_radius(RADIUS_PANEL_T);
    fill_round(x, y, x + w, y + h, r, theme_get_color_idx(COLOR_BG_PANEL));
    rect_round(x, y, x + w, y + h, r, theme_get_color_idx(COLOR_BORDER));
}
// 区块标题: 强调色小块 + 标题 + 右箭头
static void section_head(int ix, int iw, int y, const char* title, uint8_t fg, uint8_t soft, uint8_t panel, uint8_t accent) {
    fill_round(ix + 2, y + 3, ix + 12, y + 13, 3, accent);
    txt(ix + 22, y, title, fg, panel);
    ico_chev_right(ix + iw - 2, y + 8, soft);
}

void sidebar_draw(int mx, int my) {
    int p = prog();
    if (!g_target && p == 0) return;   // 关闭态: 顶栏状态簇即入口
    cc_t L; cc_measure(&L, 58 - (50 - 50 * p / 1000));
    int x = L.x, y = L.y, w = L.w, pad = L.pad, ix = x + pad + 8, iw = inner_w(&L);
    uint8_t field  = theme_get_color_idx(COLOR_FIELD);
    uint8_t fg     = theme_get_color_idx(COLOR_FG);
    uint8_t soft   = theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t accent = theme_get_color_idx(COLOR_ACCENT);
    uint8_t border = theme_get_color_idx(COLOR_BORDER);
    uint8_t panel  = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t hover  = theme_get_color_idx(COLOR_HOVER);
    uint8_t danger = theme_get_color_idx(COLOR_DANGER);
    uint8_t success= theme_get_color_idx(COLOR_SUCCESS);
    int cw = (gfx_font_scale() >= 2) ? 24 : 16;   // UI 文本格高

    // 玻璃面板: 毛玻璃(背景模糊 + 半透明) 或 8bpp/性能不足降级为半透明纯色
    int panel_r = theme_get_radius(RADIUS_PANEL_T);
    int blur = theme_get_blur_radius();
    if (blur > 0) {
        gfx_blur_rgb(x, y, x + w, y + L.h, blur);
        gfx_fill_round_rgb_alpha(x, y, x + w, y + L.h, panel_r, 16, 20, 32, 150);
    } else {
        gfx_fill_round_rgb_alpha(x, y, x + w, y + L.h, panel_r, 16, 20, 32, 214);
    }
    rect_round(x, y, x + w, y + L.h, panel_r, border);

    // ---------------- 用户卡 + 动作行 ----------------
    card(x + pad, L.top, w - pad * 2, L.top_h);

    int av_r = (L.urow_h >= 100) ? 22 : 18;
    int av_cx = ix + av_r, av_cy = L.top + L.urow_h / 2;
    uint8_t asoft = theme_get_color_idx(COLOR_ACCENT_SOFT);
    uint8_t ar, ag, ab; rgb2(asoft, &ar, &ag, &ab);
    drgb(av_cx, av_cy, av_r, ar, ag, ab);
    const char* uname = user_session_name(); if (!uname || !uname[0]) uname = "Administrator";
    char initial[2] = { uname[0], 0 };
    txt(av_cx - cjk_ui_text_w(initial) / 2, av_cy - 8, initial, fg, asoft);

    // 精简用户卡: 头像 + 用户名 + 在线绿点 + 展开箭头
    // (时间统一由顶栏承载, 移除重复的时钟/日期, 消除层级过深)
    int tx0 = av_cx + av_r + 10, tright = ix + iw;
    int row1 = L.top + 22;
    int row2 = L.top + L.urow_h - 34;
    cjk_ui_text_ellipsis(tx0, row1, uname, tright - tx0 - 20, fg, panel);
    ico_chev_right(tright - 4, row1 + cw / 2, soft);     // 展开箭头

    uint8_t sr, sg, sb; rgb2(success, &sr, &sg, &sb);
    drgb(tx0 + 3, row2 + cw / 2, 3, sr, sg, sb);
    txt(tx0 + 12, row2, "在线", success, panel);

    // 三枚动作按钮 (切换用户 / 锁定 / 电源)
    int bw = act_bw(&L), bby = act_row_y(&L), bbh = L.act_h;
    for (int i = 0; i < 3; i++) {
        int bx = act_col_x(&L, i);
        int hv = (mx >= bx && mx < bx + bw && my >= bby && my < bby + bbh);
        uint8_t lbg = hv ? hover : panel;
        if (hv) fill_round(bx, bby, bx + bw, bby + bbh, 12, hover);
        int icx = bx + bw / 2, icy = bby + (bbh - 26) / 2;
        uint8_t icol = (i == 2) ? danger : fg;
        if (i == 0) ico_person(icx, icy, icol);
        else if (i == 1) ico_lock(icx, icy, icol, lbg);
        else ico_power(icx, icy, icol, lbg);
        const char* lb = (i == 0) ? "切换用户" : (i == 1 ? "锁定" : "电源");
        int lw = cjk_ui_text_w(lb);
        txt(bx + (bw - lw) / 2, bby + bbh - 26, lb, icol, lbg);
    }

    // ---------------- 系统信息 ----------------
    card(x + pad, L.sys, w - pad * 2, L.sys_h);
    int sy = L.sys + 10;
    section_head(ix, iw, sy, "系统信息", fg, soft, panel, accent);

    int rh2 = sys_rh2(&L), rows_top = sy + 24;
    uint64_t mem_used = phys_mem_used_bytes();
    uint64_t mem_total = phys_mem_total_bytes();
    int mem_pct = mem_total ? (int)(mem_used * 100 / mem_total) : 0;
    uint64_t dsk_used = (uint64_t)g_disk_used_kb * 1024ull;
    uint64_t dsk_total = (uint64_t)g_disk_total_kb * 1024ull;
    int dsk_pct = g_disk_total_kb ? (int)((uint64_t)g_disk_used_kb * 100 / g_disk_total_kb) : 0;

    const char* rlab[2] = { "内存使用", "磁盘使用" };
    uint64_t rused[2]  = { mem_used, dsk_used };
    uint64_t rtot[2]   = { mem_total, dsk_total };
    int rpct[2]        = { mem_pct, dsk_pct };
    for (int i = 0; i < 2; i++) {
        int ry = rows_top + i * rh2;
        if (i == 0) ico_chip(ix + 8, ry + 8, soft, panel); else ico_disk(ix + 8, ry + 8, soft, panel);
        char val[40]; fmt_pair(val, rused[i], rtot[i]);
        int vw = cjk_ui_text_w(val);
        int label_w = iw - 22 - vw - 10;
        if (label_w < 40) label_w = 40;
        cjk_ui_text_ellipsis(ix + 22, ry, rlab[i], label_w, fg, panel);
        txt(ix + iw - vw, ry, val, soft, panel);
        bar(ix + 22, ry + cw, iw - 22, rpct[i], hover, accent);
    }

    // 网络连接
    int rnet = rows_top + 2 * rh2;
    ico_wifi(ix + 8, rnet + 8, 8, soft);
    txt(ix + 22, rnet, "网络连接", fg, panel);
    {
        const char* ns = g_wifi ? "已连接" : "未连接";
        uint8_t nc = g_wifi ? success : soft;
        txt(ix + iw - cjk_ui_text_w(ns), rnet, ns, nc, panel);
    }

    // ---------------- 快捷操作 ----------------
    card(x + pad, L.qk, w - pad * 2, L.qk_h);
    section_head(ix, iw, L.qk + 10, "快捷操作", fg, soft, panel, accent);

    const char* qn[3] = { "Wi-Fi", "蓝牙", "深色模式" };
    int qst[3] = { g_wifi, g_bt, g_night };
    int tx = qk_toggle_x(&L);
    for (int i = 0; i < 3; i++) {
        int ry = qk_row_y(&L, i);
        if (i == 0) ico_wifi(ix + 8, ry + 9, 7, soft);
        else if (i == 1) ico_bt(ix + 8, ry + 9, soft);
        else ico_moon(ix + 8, ry + 9, soft, panel);
        txt(ix + 22, ry, qn[i], fg, panel);
        toggle(tx, ry - 3, qst[i]);
    }

    // ---------------- 收起面板 ----------------
    fill_round(x + pad, L.foot, x + w - pad, L.foot + L.foot_h, theme_get_radius(RADIUS_CTRL_T), field);
    ico_chev(x + pad + 24, L.foot + L.foot_h / 2, -1, soft);
    txt(x + pad + 40, L.foot + (L.foot_h - cw) / 2 + 4, "收起面板", soft, field);
}

// ==================== 命中测试 ====================
int sidebar_on_click(int mx, int my) {
    int p = prog();
    if (!g_target && p == 0) {
        int bx, by, bw2, bh2; sidebar_toggle_rect(&bx, &by, &bw2, &bh2);
        if (mx >= bx && mx <= bx + bw2 && my >= by && my <= by + bh2) { sidebar_set_open(1); return 1; }
        return 0;
    }
    cc_t L; cc_measure(&L, 58 - (50 - 50 * p / 1000));
    int x = L.x, y = L.y, w = L.w;
    if (mx < x || mx > x + w || my < y || my > y + L.h) { sidebar_set_open(0); return 1; }
    // 收起面板
    if (my >= L.foot && my <= L.foot + L.foot_h) { sidebar_set_open(0); return 1; }
    // 动作按钮
    int bby = act_row_y(&L), bbh = L.act_h, bw = act_bw(&L);
    for (int i = 0; i < 3; i++) {
        int bx = act_col_x(&L, i);
        if (mx >= bx && mx < bx + bw && my >= bby && my < bby + bbh) {
            if (i == 2) desktop_poweroff();
            else wm_request_logoff();      // 切换用户 / 锁定: 回到登录界面要求重新登录
            return 1;
        }
    }
    // 快捷操作开关
    int tx = qk_toggle_x(&L);
    for (int i = 0; i < 3; i++) {
        int ry = qk_row_y(&L, i);
        if (mx >= tx && mx <= tx + 46 && my >= ry - 6 && my <= ry + 30) {
            if (i == 0) {
                g_wifi = !g_wifi;                      // Wi-Fi 联动网络状态显示
            } else if (i == 1) {
                g_bt = !g_bt;                          // 蓝牙开关
            } else {
                g_night = !g_night;                    // 深色模式: 联动系统主题并全屏重绘
                theme_set_mode(g_night ? THEME_MODE_DARK : THEME_MODE_LIGHT);
                compositor_invalidate_all();
            }
            return 1;
        }
    }
    return 1;   // 面板内其它区域: 消费点击, 不关闭
}
