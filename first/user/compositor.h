// compositor.h - FSOS 合成器与脏区域管理 (gui_framework Phase 3)
//
// 管理屏幕脏区域列表, 合成阶段仅遍历脏区域内的元素, 避免每帧全屏重绘。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.2 / 2.4.1
//
// 策略: 保守合并 (相邻间距 <= 20px 合并), 首帧/全屏变化强制 invalidate_all。
// 容量: DIRTY_REGION_MAX=32, 超限时退化为全屏重绘。
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>

// ---- 矩形区域 (x,y = 左上角, w,h = 宽高) ----
typedef struct {
    int x;
    int y;
    int w;
    int h;
} rect_t;

#define DIRTY_REGION_MAX 32

// ---- 绘制回调类型 ----
// compositor_compose 调用此回调绘制指定区域内的所有元素。
// region=NULL 表示全屏绘制。
typedef void (*draw_callback_t)(const rect_t* region);

// ==================== 接口 ====================

// 初始化合成器。注册全屏绘制回调 (wm.c 在主循环初始化时调用)。
void compositor_init(draw_callback_t draw_all);

// 标记单个矩形区域为脏 (需要重绘)。
void compositor_invalidate(int x, int y, int w, int h);

// 标记全屏为脏。
void compositor_invalidate_all(void);

// 查询是否有脏区域需要重绘。
int compositor_has_dirty(void);

// 获取脏区域列表 (返回数量, regions 数组由调用方提供)。
int compositor_get_regions(rect_t* out_regions, int max_count);

// 合并相邻脏区域 (按 x 排序, 间距 <= 20px 合并)。
void compositor_merge(void);

// 清除所有脏区域 (合成完成后调用)。
void compositor_clear(void);

// 合成: 遍历脏区域, 调用绘制回调。无脏区域时跳过。
void compositor_compose(void);

// 翻页上屏 (调用 gfx_flip)。
void compositor_flip(void);

// 强制全屏重绘并翻页 (用于退出/模式切换等场景)。
void compositor_force_full_redraw(void);

// 快照背景区域 (从后台缓冲读取指定区域像素至 dst)。
// 供毛玻璃合成: 先快照背景, 再模糊, 再叠加半透明面板。
// dst 需至少 (x1-x0+1)*(y1-y0+1)*4 字节 (32bpp)。非 LFB 路径下为空操作。
void compositor_snapshot_backdrop(int x0, int y0, int x1, int y1, uint8_t* dst);

#endif // COMPOSITOR_H