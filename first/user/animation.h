// animation.h - FSOS 动画系统 (gui_framework Phase 7)
//
// 管理短动画实例 (100~250ms), 非阻塞主循环, 定点数插值避免浮点。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.8 / 2.4.2
#ifndef ANIMATION_H
#define ANIMATION_H

#include <stdint.h>

// ---- 动画类型 ----
typedef enum {
    ANIM_WINDOW_OPEN = 0,
    ANIM_WINDOW_CLOSE,
    ANIM_WINDOW_MINIMIZE,
    ANIM_WINDOW_RESTORE,
    ANIM_STARTMENU_OPEN,
    ANIM_STARTMENU_CLOSE,
    ANIM_HOVER,
    ANIM_BUTTON_PRESS,
    ANIM_TYPE_COUNT
} anim_type_t;

// ---- 动画实例 ID ----
typedef int anim_id_t;
#define ANIM_INVALID (-1)

// ---- 动画时长约束 ----
#define ANIM_DURATION_MIN  100   // ms
#define ANIM_DURATION_MAX  250   // ms

// ---- 动画实例池容量 ----
#define ANIM_POOL_SIZE 16

// ==================== 接口 ====================

// 初始化动画系统
void anim_init(void);

// 创建动画实例。type: 动画类型, duration_ms: 时长 (钳制到 100..250)。
// 返回动画实例 ID, ANIM_INVALID 表示池满。
anim_id_t anim_create(anim_type_t type, int duration_ms);

// 取消动画实例
void anim_cancel(anim_id_t id);

// 推进所有动画实例 (每帧调用, 非阻塞)。返回 1=有动画活跃, 0=全部完成。
int  anim_tick(void);

// 查询动画是否活跃
int  anim_is_active(anim_id_t id);

// 获取动画进度 (0..1000, 定点数)。1000=完成。
int  anim_get_progress(anim_id_t id);

// 启用/禁用动画系统 (禁用时所有动画立即完成)
void anim_set_enabled(int enabled);
int  anim_get_enabled(void);

#endif // ANIMATION_H