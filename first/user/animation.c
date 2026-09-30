// animation.c - FSOS 动画系统实现 (gui_framework Phase 7)
//
// 动画实例池 + 定点数插值 (线性 + ease-out) + 非阻塞 tick。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.4.2
#include "animation.h"
#include "idt.h"       // get_ticks()

// ---- 动画实例 ----
typedef struct {
    int       active;        // 1=活跃, 0=空闲
    anim_type_t type;
    uint32_t  start_ticks;   // 起始时间 (ms)
    int       duration;      // 时长 (ms)
} anim_instance_t;

static anim_instance_t g_pool[ANIM_POOL_SIZE];
static int g_enabled = 1;

// ==================== 接口实现 ====================

void anim_init(void) {
    for (int i = 0; i < ANIM_POOL_SIZE; i++) g_pool[i].active = 0;
    g_enabled = 1;
}

anim_id_t anim_create(anim_type_t type, int duration_ms) {
    if (!g_enabled) return ANIM_INVALID;
    if (duration_ms < ANIM_DURATION_MIN) duration_ms = ANIM_DURATION_MIN;
    if (duration_ms > ANIM_DURATION_MAX) duration_ms = ANIM_DURATION_MAX;

    for (int i = 0; i < ANIM_POOL_SIZE; i++) {
        if (!g_pool[i].active) {
            g_pool[i].active = 1;
            g_pool[i].type = type;
            g_pool[i].start_ticks = get_ticks();
            g_pool[i].duration = duration_ms;
            return i;
        }
    }
    return ANIM_INVALID;
}

void anim_cancel(anim_id_t id) {
    if (id < 0 || id >= ANIM_POOL_SIZE) return;
    g_pool[id].active = 0;
}

int anim_tick(void) {
    if (!g_enabled) return 0;
    uint32_t now = get_ticks();
    int any_active = 0;
    for (int i = 0; i < ANIM_POOL_SIZE; i++) {
        if (g_pool[i].active) {
            uint32_t elapsed = now - g_pool[i].start_ticks;
            if ((int)elapsed >= g_pool[i].duration) {
                g_pool[i].active = 0;  // 动画完成
            } else {
                any_active = 1;
            }
        }
    }
    return any_active;
}

int anim_is_active(anim_id_t id) {
    if (id < 0 || id >= ANIM_POOL_SIZE) return 0;
    return g_pool[id].active;
}

int anim_get_progress(anim_id_t id) {
    if (id < 0 || id >= ANIM_POOL_SIZE) return 1000;
    if (!g_pool[id].active) return 1000;
    uint32_t now = get_ticks();
    uint32_t elapsed = now - g_pool[id].start_ticks;
    int dur = g_pool[id].duration;
    if (dur <= 0) return 1000;
    int progress = (int)(elapsed * 1000 / (uint32_t)dur);
    if (progress > 1000) progress = 1000;
    // ease-out 缓动: progress = 1 - (1-t)^2
    // 定点数: t = progress/1000, result = 1000 - (1000-t)^2/1000
    int t = progress;
    int inv = 1000 - t;
    int eased = 1000 - (inv * inv) / 1000;
    return eased;
}

void anim_set_enabled(int enabled) {
    g_enabled = enabled ? 1 : 0;
    if (!g_enabled) {
        // 禁用时所有动画立即完成
        for (int i = 0; i < ANIM_POOL_SIZE; i++) g_pool[i].active = 0;
    }
}

int anim_get_enabled(void) {
    return g_enabled;
}