// control.c - FSOS 统一控件系统实现 (gui_framework Phase 8)
//
// 5 态状态机 + 焦点管理器 + 控件绘制 (Theme API 驱动)。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.1.9
#include "control.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "theme.h"
#include "theme_api.h"
#include "kb.h"

// ---- 焦点管理器内部状态 ----
#define FOCUS_MAX 32
static control_t* g_focus_chain[FOCUS_MAX];
static int g_focus_count = 0;
static int g_focus_idx = -1;

// ---- 内部绘制助手 ----
static void fill_round(int x0, int y0, int x1, int y1, int r, uint8_t c) {
    if (gfx_is_lfb()) {
        uint8_t cr, cg, cb; gfx_idx_rgb(c, &cr, &cg, &cb);
        gfx_fill_round_rgb_aa(x0, y0, x1, y1, r, cr, cg, cb);
    } else gfx_fill_round_idx(x0, y0, x1, y1, r, c);
}
static void rect_round(int x0, int y0, int x1, int y1, int r, uint8_t c) {
    if (gfx_is_lfb()) {
        uint8_t cr, cg, cb; gfx_idx_rgb(c, &cr, &cg, &cb);
        gfx_round_rect_rgb_aa(x0, y0, x1, y1, r, cr, cg, cb);
    } else gfx_round_rect_idx(x0, y0, x1, y1, r, c);
}

// ==================== 控件生命周期 ====================

void control_init(control_t* c, control_type_t type, int id,
                  int x, int y, int w, int h, const char* text) {
    c->type = type;
    c->state = CTRL_STATE_NORMAL;
    c->id = id;
    c->x = x; c->y = y; c->w = w; c->h = h;
    c->text = text;
    c->value = 0;
    c->min_val = 0; c->max_val = 100;
    c->visible = 1;
    c->enabled = 1;
    c->on_click = 0;
    c->on_change = 0;
}

void control_set_state(control_t* c, control_state_t state) {
    if (!c->enabled) { c->state = CTRL_STATE_DISABLED; return; }
    c->state = state;
}

void control_set_enabled(control_t* c, int enabled) {
    c->enabled = enabled ? 1 : 0;
    if (!c->enabled) c->state = CTRL_STATE_DISABLED;
    else if (c->state == CTRL_STATE_DISABLED) c->state = CTRL_STATE_NORMAL;
}

control_state_t control_get_state(control_t* c) {
    return c->state;
}

// ==================== 控件绘制 ====================

void control_draw(control_t* c) {
    if (!c->visible) return;

    uint8_t bg, fg, border;
    switch (c->state) {
        case CTRL_STATE_HOVER:
            bg = theme_get_color_idx(COLOR_HOVER);
            fg = theme_get_color_idx(COLOR_FG);
            border = theme_get_color_idx(COLOR_ACCENT_SOFT);
            break;
        case CTRL_STATE_PRESSED:
            bg = theme_get_color_idx(COLOR_PRESSED);
            fg = theme_get_color_idx(COLOR_FG_TITLE);
            border = theme_get_color_idx(COLOR_ACCENT);
            break;
        case CTRL_STATE_FOCUSED:
            bg = theme_get_color_idx(COLOR_BG_PANEL);
            fg = theme_get_color_idx(COLOR_FG);
            border = theme_get_color_idx(COLOR_BORDER_FOCUS);
            break;
        case CTRL_STATE_DISABLED:
            bg = theme_get_color_idx(COLOR_DISABLED);
            fg = theme_get_color_idx(COLOR_FG_SOFT);
            border = theme_get_color_idx(COLOR_BORDER);
            break;
        default:
            bg = theme_get_color_idx(COLOR_BG_PANEL);
            fg = theme_get_color_idx(COLOR_FG);
            border = theme_get_color_idx(COLOR_BORDER);
            break;
    }

    int r = theme_get_radius(RADIUS_CTRL_T);

    switch (c->type) {
        case CTRL_BUTTON:
        case CTRL_ICONBUTTON:
            fill_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r, bg);
            rect_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r, border);
            if (c->text) cjk_text(c->x + 6, c->y + 2, c->text, fg, bg);
            break;
        case CTRL_TEXTBOX:
            fill_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r,
                       theme_get_color_idx(COLOR_FIELD));
            rect_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r, border);
            if (c->text) cjk_text(c->x + 4, c->y + 2, c->text, fg, theme_get_color_idx(COLOR_FIELD));
            break;
        case CTRL_LABEL:
            if (c->text) cjk_text(c->x, c->y, c->text, fg, bg);
            break;
        case CTRL_PANEL:
            fill_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r, bg);
            rect_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r, border);
            break;
        case CTRL_SEPARATOR:
            gfx_fill_idx(c->x, c->y, c->x + c->w - 1, c->y, border);
            break;
        default:
            fill_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r, bg);
            rect_round(c->x, c->y, c->x + c->w - 1, c->y + c->h - 1, r, border);
            break;
    }
}

// ==================== 控件事件 ====================

int control_on_mouse(control_t* c, int mx, int my, int ldown) {
    if (!c->visible || !c->enabled) return 0;
    int inside = (mx >= c->x && mx < c->x + c->w && my >= c->y && my < c->y + c->h);
    if (ldown && inside) {
        control_set_state(c, CTRL_STATE_PRESSED);
        return 1;
    } else if (inside) {
        control_set_state(c, CTRL_STATE_HOVER);
        return 1;
    } else {
        control_set_state(c, CTRL_STATE_NORMAL);
        return 0;
    }
}

int control_on_key(control_t* c, int key) {
    if (!c->visible || !c->enabled) return 0;
    if (key == ' ' || key == KEY_ENTER) {
        if (c->type == CTRL_CHECKBOX || c->type == CTRL_RADIO) {
            c->value = !c->value;
            c->state = CTRL_STATE_FOCUSED;
            if (c->on_change) c->on_change(c);
            return 1;
        }
        if (c->type == CTRL_BUTTON || c->type == CTRL_ICONBUTTON) {
            c->state = CTRL_STATE_PRESSED;
            if (c->on_click) c->on_click(c);
            return 1;
        }
    }
    if (key == KEY_TAB || key == KEY_DOWN || key == KEY_RIGHT) {
        focus_manager_next();
        return 1;
    }
    if (key == KEY_UP || key == KEY_LEFT) {
        focus_manager_prev();
        return 1;
    }
    if (key == KEY_ESC) {
        c->state = CTRL_STATE_NORMAL;
        return 0;
    }
    return 0;
}

// ==================== 焦点管理器 ====================

void focus_manager_init(void) {
    g_focus_count = 0;
    g_focus_idx = -1;
}

void focus_manager_add(control_t* c) {
    if (g_focus_count >= FOCUS_MAX) return;
    g_focus_chain[g_focus_count++] = c;
}

void focus_manager_remove(control_t* c) {
    for (int i = 0; i < g_focus_count; i++) {
        if (g_focus_chain[i] == c) {
            g_focus_chain[i] = g_focus_chain[g_focus_count - 1];
            g_focus_count--;
            if (g_focus_idx >= g_focus_count) g_focus_idx = g_focus_count - 1;
            return;
        }
    }
}

void focus_manager_next(void) {
    if (g_focus_count == 0) return;
    if (g_focus_idx >= 0 && g_focus_idx < g_focus_count)
        control_set_state(g_focus_chain[g_focus_idx], CTRL_STATE_NORMAL);
    g_focus_idx = (g_focus_idx + 1) % g_focus_count;
    control_set_state(g_focus_chain[g_focus_idx], CTRL_STATE_FOCUSED);
}

void focus_manager_prev(void) {
    if (g_focus_count == 0) return;
    if (g_focus_idx >= 0 && g_focus_idx < g_focus_count)
        control_set_state(g_focus_chain[g_focus_idx], CTRL_STATE_NORMAL);
    g_focus_idx = (g_focus_idx - 1 + g_focus_count) % g_focus_count;
    control_set_state(g_focus_chain[g_focus_idx], CTRL_STATE_FOCUSED);
}

control_t* focus_manager_get(void) {
    if (g_focus_idx < 0 || g_focus_idx >= g_focus_count) return 0;
    return g_focus_chain[g_focus_idx];
}

void focus_manager_clear(void) {
    g_focus_count = 0;
    g_focus_idx = -1;
}