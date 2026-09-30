// control.h - FSOS 统一控件系统 (gui_framework Phase 8)
//
// 14 控件类型 × 5 态 (NORMAL/HOVER/PRESSED/FOCUSED/DISABLED) + 焦点管理器。
// gui.c 旧控件委托至本模块。设计依据: design.md 2.1.9
#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

// ---- 控件类型 ----
typedef enum {
    CTRL_BUTTON = 0,
    CTRL_TEXTBOX,
    CTRL_LABEL,
    CTRL_CHECKBOX,
    CTRL_RADIO,
    CTRL_SLIDER,
    CTRL_LISTBOX,
    CTRL_COMBOBOX,
    CTRL_TAB,
    CTRL_PANEL,
    CTRL_SEPARATOR,
    CTRL_IMAGE,
    CTRL_ICONBUTTON,
    CTRL_PROGRESSBAR,
    CTRL_TYPE_COUNT
} control_type_t;

// ---- 控件状态 (5 态) ----
typedef enum {
    CTRL_STATE_NORMAL = 0,
    CTRL_STATE_HOVER,
    CTRL_STATE_PRESSED,
    CTRL_STATE_FOCUSED,
    CTRL_STATE_DISABLED,
    CTRL_STATE_COUNT
} control_state_t;

// ---- 控件结构体 ----
typedef struct control_s {
    control_type_t  type;
    control_state_t state;
    int      id;             // 控件 ID (供事件处理)
    int      x, y, w, h;     // 几何
    const char* text;        // 文本/标签
    int      value;          // 当前值 (checkbox/radio/slider)
    int      min_val, max_val;  // 范围 (slider)
    int      visible;        // 1=可见
    int      enabled;        // 1=可用, 0=禁用
    void    (*on_click)(struct control_s* self);
    void    (*on_change)(struct control_s* self);
} control_t;

// ==================== 控件生命周期 ====================

void control_init(control_t* c, control_type_t type, int id,
                  int x, int y, int w, int h, const char* text);
void control_set_state(control_t* c, control_state_t state);
void control_set_enabled(control_t* c, int enabled);
control_state_t control_get_state(control_t* c);

// ==================== 控件绘制 ====================

void control_draw(control_t* c);

// ==================== 控件事件 ====================

// 返回 1=事件已消费
int  control_on_mouse(control_t* c, int mx, int my, int ldown);
int  control_on_key(control_t* c, int key);

// ==================== 焦点管理器 ====================

void focus_manager_init(void);
void focus_manager_add(control_t* c);
void focus_manager_remove(control_t* c);
void focus_manager_next(void);      // Tab
void focus_manager_prev(void);      // Shift+Tab
control_t* focus_manager_get(void);
void focus_manager_clear(void);

#endif // CONTROL_H