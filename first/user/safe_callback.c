// safe_callback.c - FSOS 应用回调异常隔离实现 (gui_framework Phase 3)
//
// 返回值检查方案: 调用前验证 app 有效性, 调用后验证 app 状态一致。
// 若回调导致 app->open=0 (应用自行关闭), 标记需重绘。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.4.4
#include "safe_callback.h"

// ---- 内部有效性检查 ----
static int app_valid(app_t* a) {
    return a != 0 && a->open && !a->minimized;
}

// ---- 安全回调实现 ----

void safe_callback_draw(app_t* a, int x, int y, int w, int h) {
    if (!app_valid(a)) return;
    if (!a->draw) return;
    if (w <= 0 || h <= 0) return;
    a->draw(x, y, w, h);
}

int safe_callback_on_key(app_t* a, int k) {
    if (!app_valid(a)) return 0;
    if (!a->on_key) return 0;
    return a->on_key(k);
}

int safe_callback_on_mouse(app_t* a, int x, int y, int ldown) {
    if (!app_valid(a)) return 0;
    if (!a->on_mouse) return 0;
    return a->on_mouse(x, y, ldown);
}

int safe_callback_on_tick(app_t* a) {
    if (!app_valid(a)) return 0;
    if (!a->on_tick) return 0;
    return a->on_tick();
}

int safe_callback_on_rmouse(app_t* a, int mx, int my) {
    if (!app_valid(a)) return 0;
    if (!a->on_rmouse) return 0;
    return a->on_rmouse(mx, my);
}