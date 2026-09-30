// event_queue.h - FSOS GUI 事件队列 (gui_framework Phase 3)
//
// 从输入驱动 (kb/mouse) 采集原始事件, 有序缓冲, 按优先级分发。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.5 / 2.4.3
//
// 优先级: 上下文菜单 > 开始菜单 > 任务栏 > 窗口 > 桌面
// 容量: EVENT_QUEUE_SIZE=64, 环形缓冲, 满时丢弃最旧事件。
#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdint.h>

// ---- 事件类型 ----
typedef enum {
    EV_NONE = 0,
    EV_MOUSE_MOVE,
    EV_MOUSE_LEFT,         // 左键按下边沿
    EV_MOUSE_RIGHT,        // 右键按下边沿
    EV_MOUSE_DOUBLE_CLICK,
    EV_MOUSE_SCROLL,
    EV_KEY,                // 键盘按键
} event_type_t;

// ---- GUI 事件 ----
typedef struct {
    event_type_t type;
    int    x, y;           // 鼠标坐标 (键盘事件时为光标位置)
    int    key;            // 键盘事件: 按键码; 鼠标事件: 0
    int    pressed;        // 1=按下边沿, 0=释放
    uint32_t timestamp;    // 事件时间戳 (get_ticks())
} gui_event_t;

#define EVENT_QUEUE_SIZE 64

// ---- 分发优先级 (数值越小优先级越高) ----
typedef enum {
    PRIO_CTX_MENU = 0,     // 上下文菜单 (最高)
    PRIO_START_MENU,       // 开始菜单
    PRIO_TASKBAR,          // 任务栏 / Dock
    PRIO_WINDOW,           // 窗口
    PRIO_DESKTOP,          // 桌面图标
    PRIO_KEYBOARD,         // 键盘 (最低: 鼠标处理器对 EV_KEY 返回 0, 事件落到此处)
    PRIO_COUNT
} event_priority_t;

// 事件处理器: 返回 1=已消费 (停止向低优先级传递), 0=未消费。
typedef int (*event_handler_fn)(const gui_event_t* ev);

// ==================== 接口 ====================

// 初始化事件队列
void event_queue_init(void);

// 注册事件处理器 (同一优先级仅保留最后注册的)
void event_queue_register_handler(event_priority_t prio, event_handler_fn fn);

// 推入事件 (满时丢弃最旧)
void event_queue_push(const gui_event_t* ev);

// 弹出事件 (返回 1=有事件, 0=空)
int  event_queue_pop(gui_event_t* out);

// 查询队列中事件数量
int  event_queue_count(void);

// 从输入驱动采集原始事件入队 (kb_poll + mouse_get 差分)
// 返回采集到的事件数
int  event_queue_poll_from_drivers(void);

// 重置鼠标边沿检测基线 (跳过一帧/应用切换后调用, 避免把状态突变误判为点击)
void event_queue_sync_mouse(void);

// 分发队列中所有事件 (按优先级调用注册的处理器, 直至某处理器返回 1; 分发后清空队列)
void event_queue_dispatch(void);

#endif // EVENT_QUEUE_H