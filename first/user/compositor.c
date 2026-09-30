// compositor.c - FSOS 合成器与脏区域管理实现 (gui_framework Phase 3)
//
// 脏区域列表 (静态数组 32 项) + 相邻合并算法 (O(n²) n<=32)。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.4.1
#include "compositor.h"
#include "gfx.h"       // gfx_flip

// ---- 内部状态 ----
static rect_t  g_regions[DIRTY_REGION_MAX];
static int     g_count = 0;
static int     g_full_invalidate = 0;  // 全屏脏标记
static draw_callback_t g_draw_all = 0;

// 屏幕尺寸缓存 (避免每帧查询)
static int g_screen_w = 0;
static int g_screen_h = 0;

// ---- 矩形辅助 ----
static int rects_overlap(const rect_t* a, const rect_t* b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

static rect_t rect_union(const rect_t* a, const rect_t* b) {
    rect_t r;
    int x0 = (a->x < b->x) ? a->x : b->x;
    int y0 = (a->y < b->y) ? a->y : b->y;
    int x1 = a->x + a->w; if (b->x + b->w > x1) x1 = b->x + b->w;
    int y1 = a->y + a->h; if (b->y + b->h > y1) y1 = b->y + b->h;
    r.x = x0; r.y = y0; r.w = x1 - x0; r.h = y1 - y0;
    return r;
}

// ==================== 接口实现 ====================

void compositor_init(draw_callback_t draw_all) {
    g_count = 0;
    g_full_invalidate = 1;  // 首帧强制全屏
    g_draw_all = draw_all;
    g_screen_w = gfx_width();
    g_screen_h = gfx_height();
}

void compositor_invalidate(int x, int y, int w, int h) {
    if (g_full_invalidate) return;  // 已全屏脏, 无需添加
    if (w <= 0 || h <= 0) return;

    // 钳制到屏幕范围
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_screen_w) w = g_screen_w - x;
    if (y + h > g_screen_h) h = g_screen_h - y;
    if (w <= 0 || h <= 0) return;

    if (g_count >= DIRTY_REGION_MAX) {
        // 超限: 退化为全屏
        g_full_invalidate = 1;
        g_count = 0;
        return;
    }
    g_regions[g_count].x = x;
    g_regions[g_count].y = y;
    g_regions[g_count].w = w;
    g_regions[g_count].h = h;
    g_count++;
}

void compositor_invalidate_all(void) {
    g_full_invalidate = 1;
    g_count = 0;
}

int compositor_has_dirty(void) {
    return g_full_invalidate || g_count > 0;
}

int compositor_get_regions(rect_t* out_regions, int max_count) {
    if (g_full_invalidate) {
        if (max_count >= 1) {
            out_regions[0].x = 0;
            out_regions[0].y = 0;
            out_regions[0].w = g_screen_w;
            out_regions[0].h = g_screen_h;
        }
        return 1;
    }
    int n = (g_count < max_count) ? g_count : max_count;
    for (int i = 0; i < n; i++) out_regions[i] = g_regions[i];
    return n;
}

void compositor_merge(void) {
    if (g_full_invalidate || g_count <= 1) return;

    // 简单合并: 遍历所有对, 重叠或相邻 (间距 <= 20px) 则合并
    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < g_count; i++) {
            for (int j = i + 1; j < g_count; j++) {
                rect_t* a = &g_regions[i];
                rect_t* b = &g_regions[j];
                // 扩展 a 的边界 20px 检查是否与 b 相邻
                rect_t a_exp = *a;
                a_exp.x -= 20; a_exp.y -= 20;
                a_exp.w += 40; a_exp.h += 40;
                if (rects_overlap(&a_exp, b)) {
                    g_regions[i] = rect_union(a, b);
                    // 移除 j
                    g_regions[j] = g_regions[g_count - 1];
                    g_count--;
                    merged = 1;
                    if (g_count <= 1) return;
                    break;
                }
            }
            if (merged) break;
        }
    }
}

void compositor_clear(void) {
    g_count = 0;
    g_full_invalidate = 0;
}

void compositor_compose(void) {
    if (!g_draw_all) return;
    if (!compositor_has_dirty()) return;

    if (g_full_invalidate) {
        // 全屏绘制
        g_draw_all(0);
    } else {
        // 合并后遍历脏区域绘制
        compositor_merge();
        for (int i = 0; i < g_count; i++) {
            g_draw_all(&g_regions[i]);
        }
    }
    compositor_clear();
}

void compositor_flip(void) {
    gfx_flip();
}

void compositor_force_full_redraw(void) {
    compositor_invalidate_all();
    compositor_compose();
    compositor_flip();
}
void compositor_snapshot_backdrop(int x0, int y0, int x1, int y1, uint8_t* dst) {
    if (!dst) return;
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;
    if (w <= 0 || h <= 0) return;
    // 委托 gfx 读取后台缓冲像素至 dst
    // 通过 gfx_pixel_rgb 逐像素读取太慢, 直接读 g_back 需访问 gfx 内部状态。
    // 改用 gfx 提供的快照接口: 逐行 memcpy 从后台缓冲。
    // 但 gfx 内部缓冲是 static, 无法直接访问。使用 gfx_read_rect 代理。
    // 简化方案: 逐像素用 gfx_is_lfb 判断, LFB 读后台缓冲需 gfx 暴露接口。
    // 当前实现: 留空, 由 gfx_blur_rgb 直接在后台缓冲上操作, 无需显式快照。
    (void)w; (void)h;
}