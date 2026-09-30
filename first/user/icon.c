// icon.c - 应用图标加载与绘制
// 双路径: 磁盘文件区 (icon_draw) + 程序化内嵌回退 (icon_draw_builtin)
#include "icon.h"
#include "vga.h"
#include "theme.h"      // ICN_RADIUS, RADIUS_CTRL (modern_ui 图标模板)
#include "filesys.h"

// ---- 磁盘加载 ----
// 越界索引降级: 未登记索引映射到最近登记色, 防止乱码 (spec 5.1.3.1)
static uint8_t icon_sanitize(uint8_t idx) {
    if (idx <= COL_UI_DOCK_HI_SOFT) return idx;  // 0..165 均已登记
    return COL_DGRAY;                              // 166..255 退化为深灰
}

int icon_draw(const char* name, int x, int y) {
    static char buf[FS_MAX_SIZE];
    int n = fs_read(name, buf, FS_MAX_SIZE);
    if (n < 8) return 0;
    if (buf[0] != 'I' || buf[1] != 'C' || buf[2] != 'N' || buf[3] != '1') return 0;
    int w = (uint8_t)buf[4] | ((uint8_t)buf[5] << 8);
    int h = (uint8_t)buf[6] | ((uint8_t)buf[7] << 8);
    if (w <= 0 || h <= 0 || w > 64 || h > 64) return 0;
    if (n < 8 + w * h) return 0;
    const uint8_t* px = (const uint8_t*)(buf + 8);
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            vga_pixel(x + col, y + row, icon_sanitize(px[row * w + col]));
    return 1;
}

// ---- 程序化绘制辅助 (AA 双模式分派) ----
static void icon_bg(int x, int y, uint8_t c) {
    if (gfx_is_lfb()) {
        uint8_t cr, cg, cb; gfx_idx_rgb(c, &cr, &cg, &cb);
        gfx_fill_round_rgb_aa(x, y, x + ICON_SIZE - 1, y + ICON_SIZE - 1, ICN_RADIUS, cr, cg, cb);
    } else {
        vga_fill_round_rect(x, y, x + ICON_SIZE - 1, y + ICON_SIZE - 1, ICN_RADIUS, c);
    }
}
static void icon_border(int x, int y, uint8_t c) {
    if (gfx_is_lfb()) {
        uint8_t cr, cg, cb; gfx_idx_rgb(c, &cr, &cg, &cb);
        gfx_round_rect_rgb_aa(x, y, x + ICON_SIZE - 1, y + ICON_SIZE - 1, ICN_RADIUS, cr, cg, cb);
    } else {
        vga_draw_round_rect(x, y, x + ICON_SIZE - 1, y + ICON_SIZE - 1, ICN_RADIUS, c);
    }
}
// 画实心圆 (AA in LFB, 索引 in 8bpp)
static void icon_disc(int cx, int cy, int r, uint8_t c) {
    if (gfx_is_lfb()) {
        uint8_t cr, cg, cb; gfx_idx_rgb(c, &cr, &cg, &cb);
        gfx_disc_aa(cx, cy, r, cr, cg, cb);
    } else {
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx * dx + dy * dy <= r * r)
                    vga_pixel(cx + dx, cy + dy, c);
    }
}
// 画圆环 (外径 r, 线宽 1)
static void icon_ring(int cx, int cy, int r, uint8_t c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= (r - 1) * (r - 1))
                vga_pixel(cx + dx, cy + dy, c);
        }
}

// ---- 6 种程序化图标 ----
static void icon_terminal(int x, int y) {
    icon_bg(x, y, COL_BLACK);
    vga_fill_rect(x, y, x + 31, y + 6, COL_TITLEBG);      // 标题栏
    vga_fill_rect(x + 27, y + 2, x + 29, y + 4, COL_LRED); // 关闭按钮
    vga_draw_text(x + 3, y + 9, ">_", COL_LGREEN, COL_BLACK);
    vga_draw_text(x + 3, y + 19, "$_", COL_WHITE, COL_BLACK);
    icon_border(x, y, COL_LGRAY);
}
static void icon_usermgr(int x, int y) {
    icon_bg(x, y, COL_PANEL);
    icon_disc(x + 16, y + 11, 6, COL_LBLUE);              // 头
    vga_fill_rect(x + 8, y + 17, x + 24, y + 27, COL_LBLUE); // 身
    vga_fill_rect(x + 10, y + 27, x + 22, y + 28, COL_ACCENT); // 底座
    icon_border(x, y, COL_WHITE);
}
static void icon_desktop(int x, int y) {
    icon_bg(x, y, COL_PANEL);
    vga_fill_rect(x + 3, y + 4, x + 28, y + 8, COL_ACCENT);  // 标题栏
    vga_fill_rect(x + 3, y + 9, x + 28, y + 27, COL_WHITE);  // 内容区
    vga_fill_rect(x + 6, y + 12, x + 16, y + 15, COL_LGRAY); // 文字行1
    vga_fill_rect(x + 6, y + 18, x + 25, y + 21, COL_LGRAY); // 文字行2
    vga_fill_rect(x + 6, y + 24, x + 20, y + 25, COL_LGRAY); // 文字行3
    icon_border(x, y, COL_SHADOW);
}
static void icon_taskmgr(int x, int y) {
    icon_bg(x, y, COL_PANEL);
    vga_fill_rect(x + 6,  y + 18, x + 10, y + 26, COL_LGREEN); // 矮柱
    vga_fill_rect(x + 13, y + 12, x + 17, y + 26, COL_YELLOW); // 中柱
    vga_fill_rect(x + 20, y + 6,  x + 24, y + 26, COL_LRED);   // 高柱
    vga_draw_rect(x + 4, y + 4, x + 27, y + 27, COL_LGRAY);    // 外框
    icon_border(x, y, COL_SHADOW);
}
static void icon_lock(int x, int y) {
    icon_bg(x, y, COL_PANEL);
    icon_ring(x + 16, y + 11, 5, COL_YELLOW);             // 锁环
    vga_fill_rect(x + 16, y + 6, x + 16, y + 11, COL_PANEL); // 环底开口(擦)
    vga_fill_rect(x + 10, y + 12, x + 22, y + 26, COL_YELLOW); // 锁身
    vga_fill_rect(x + 14, y + 17, x + 18, y + 22, COL_BLACK);  // 锁孔
    icon_border(x, y, COL_WHITE);
}
static void icon_software(int x, int y) {
    icon_bg(x, y, COL_PANEL);
    vga_fill_rect(x + 5, y + 5, x + 27, y + 27, COL_LGRAY);   // 软盘外壳
    vga_fill_rect(x + 8, y + 5,  x + 24, y + 12, COL_WHITE);  // 标签
    vga_fill_rect(x + 10, y + 7, x + 22, y + 9, COL_DGRAY);   // 标签文字
    vga_fill_rect(x + 10, y + 18, x + 22, y + 25, COL_DGRAY); // 读写窗
    vga_fill_rect(x + 18, y + 18, x + 22, y + 20, COL_WHITE); // 窗口高亮
    icon_border(x, y, COL_SHADOW);
}

int icon_draw_builtin(int id, int x, int y) {
    switch (id) {
        case ICON_TERMINAL: icon_terminal(x, y); break;
        case ICON_USERMGR:  icon_usermgr(x, y);  break;
        case ICON_DESKTOP:  icon_desktop(x, y);  break;
        case ICON_TASKMGR:  icon_taskmgr(x, y);  break;
        case ICON_LOCK:     icon_lock(x, y);     break;
        case ICON_SOFTWARE: icon_software(x, y); break;
        default: icon_bg(x, y, COL_DGRAY); icon_border(x, y, COL_LGRAY); break;
    }
    return 1;
}

int icon_draw_auto(const char* name, int id, int x, int y) {
    if (name && icon_draw(name, x, y)) return 1;
    return icon_draw_builtin(id, x, y);
}
// 统一图标模板渲染 (modern_ui): 委托内嵌图标绘制, AA/混色分派在 icon_bg/border/disc 内集中.
// kind 0..5 映射到 ICON_* 内嵌图标; kind 6+ 保留扩展 (wm 桌面图标暂留 wm.c draw_icon).
void icon_render(int kind, int x, int y) {
    icon_draw_builtin(kind, x, y);
}