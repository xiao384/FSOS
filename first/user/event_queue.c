// event_queue.c - FSOS GUI 事件队列实现 (gui_framework Phase 3)
//
// 环形缓冲队列 + 从输入驱动采集 + 优先级分发框架。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.4.3
#include "event_queue.h"
#include "kb.h"
#include "mouse.h"
#include "idt.h"      // get_ticks()

// ---- 内部状态 ----
static gui_event_t g_events[EVENT_QUEUE_SIZE];
static int g_head = 0;   // 弹出位置
static int g_tail = 0;   // 推入位置
static int g_count = 0;  // 当前事件数

// 上次鼠标状态 (用于边沿检测)
static mouse_state_t g_prev_mouse = {0};
static int g_prev_mouse_valid = 0;

// 优先级处理器表 (同一优先级仅保留最后注册的)
static event_handler_fn g_handlers[PRIO_COUNT];

// ==================== 接口实现 ====================

void event_queue_init(void) {
    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_prev_mouse_valid = 0;
    for (int i = 0; i < PRIO_COUNT; i++) g_handlers[i] = 0;
}

void event_queue_register_handler(event_priority_t prio, event_handler_fn fn) {
    if (prio < 0 || prio >= PRIO_COUNT) return;
    g_handlers[prio] = fn;
}

void event_queue_push(const gui_event_t* ev) {
    if (g_count >= EVENT_QUEUE_SIZE) {
        // 满时丢弃最旧 (head 前进)
        g_head = (g_head + 1) % EVENT_QUEUE_SIZE;
        g_count--;
    }
    g_events[g_tail] = *ev;
    g_tail = (g_tail + 1) % EVENT_QUEUE_SIZE;
    g_count++;
}

int event_queue_pop(gui_event_t* out) {
    if (g_count == 0) return 0;
    *out = g_events[g_head];
    g_head = (g_head + 1) % EVENT_QUEUE_SIZE;
    g_count--;
    return 1;
}

int event_queue_count(void) {
    return g_count;
}

// 重置边沿基线: 下一次 poll 仅记录当前鼠标状态, 不产生点击事件
void event_queue_sync_mouse(void) {
    g_prev_mouse_valid = 0;
}

int event_queue_poll_from_drivers(void) {
    int collected = 0;

    // ---- 鼠标事件 ----
    mouse_state_t m;
    mouse_get(&m);

    if (!g_prev_mouse_valid) {
        g_prev_mouse = m;
        g_prev_mouse_valid = 1;
        return 0;
    }

    // 鼠标移动 (高频节流: 若队尾已是移动事件则仅更新其坐标, 不追加)
    if (m.x != g_prev_mouse.x || m.y != g_prev_mouse.y) {
        int merged = 0;
        if (g_count > 0) {
            int last = (g_tail - 1 + EVENT_QUEUE_SIZE) % EVENT_QUEUE_SIZE;
            if (g_events[last].type == EV_MOUSE_MOVE) {
                g_events[last].x = m.x;
                g_events[last].y = m.y;
                g_events[last].timestamp = get_ticks();
                merged = 1;
            }
        }
        if (!merged) {
            gui_event_t ev = {0};
            ev.type = EV_MOUSE_MOVE;
            ev.x = m.x;
            ev.y = m.y;
            ev.timestamp = get_ticks();
            event_queue_push(&ev);
            collected++;
        }
    }

    // 左键按下边沿
    if (m.left && !g_prev_mouse.left) {
        gui_event_t ev = {0};
        ev.type = EV_MOUSE_LEFT;
        ev.x = m.x;
        ev.y = m.y;
        ev.pressed = 1;
        ev.timestamp = get_ticks();
        event_queue_push(&ev);
        collected++;
    }

    // 右键按下边沿
    if (m.right && !g_prev_mouse.right) {
        gui_event_t ev = {0};
        ev.type = EV_MOUSE_RIGHT;
        ev.x = m.x;
        ev.y = m.y;
        ev.pressed = 1;
        ev.timestamp = get_ticks();
        event_queue_push(&ev);
        collected++;
    }

    g_prev_mouse = m;

    // ---- 键盘事件 ----
    int k = kb_poll();
    if (k) {
        gui_event_t ev = {0};
        ev.type = EV_KEY;
        ev.key = k;
        ev.x = m.x;
        ev.y = m.y;
        ev.pressed = 1;
        ev.timestamp = get_ticks();
        event_queue_push(&ev);
        collected++;
    }

    return collected;
}

void event_queue_dispatch(void) {
    // 按优先级依次调用注册的处理器, 直至某处理器返回 1 (已消费); 分发后清空队列。
    gui_event_t ev;
    while (event_queue_pop(&ev)) {
        for (int p = 0; p < PRIO_COUNT; p++) {
            if (g_handlers[p] && g_handlers[p](&ev)) break;
        }
    }
}