// safe_callback.h - FSOS 应用回调异常隔离 (gui_framework Phase 3)
//
// 包装 app_t 回调 (draw/on_key/on_mouse/on_tick/on_rmouse), 防止无效应用
// 回调导致桌面崩溃。裸机环境 setjmp/longjmp 不可靠, 采用返回值检查方案。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.4.4
#ifndef SAFE_CALLBACK_H
#define SAFE_CALLBACK_H

#include "window.h"    // app_t

// ---- 安全回调包装 ----
// 每个包装函数: 检查 app 有效性 (非 NULL + open + !minimized) → 调用回调 → 检查结果

// 绘制回调 (传入客户区几何 x,y,w,h; 内部校验 app 有效性与回调非空)
void safe_callback_draw(app_t* a, int x, int y, int w, int h);

// 键盘回调 (返回 1=已消费)
int  safe_callback_on_key(app_t* a, int k);

// 鼠标回调 (返回值透传)
int  safe_callback_on_mouse(app_t* a, int x, int y, int ldown);

// 周期回调 (返回 1=需要重绘)
int  safe_callback_on_tick(app_t* a);

// 右键回调 (返回值透传)
int  safe_callback_on_rmouse(app_t* a, int mx, int my);

#endif // SAFE_CALLBACK_H