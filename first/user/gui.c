// gui.c - 图形界面组件与对话框框架
// 屏幕 320x200, 8x8 字符, 键盘导航
#include "gui.h"
#include "kb.h"
#include "cjk.h"      // ui_polish: 中文文案绘制
#include "theme.h"    // ui_polish: 圆角半径/对比组合

void gui_title_bar(const char* left, const char* right) {
    uint8_t bg_title = theme_get_color_idx(COLOR_BG_TITLE);
    vga_fill_rect(0, 0, VGA_W - 1, 15, bg_title);
    vga_draw_rect(0, 0, VGA_W - 1, 15, theme_get_color_idx(COLOR_BORDER));
    cjk_text(4, 0, left, theme_get_color_idx(COLOR_FG_TITLE), bg_title);
    if (right && right[0]) {
        int w = cjk_text_w(right);
        cjk_text(VGA_W - 4 - w, 0, right, theme_get_color_idx(COLOR_FG_SOFT), bg_title);
    }
}

void gui_status_bar(const char* msg, uint8_t color) {
    int y0 = SCREEN_H - 16, y1 = SCREEN_H - 1;
    uint8_t bg_title = theme_get_color_idx(COLOR_BG_TITLE);
    vga_fill_rect(0, y0, VGA_W - 1, y1, bg_title);
    vga_draw_rect(0, y0, VGA_W - 1, y1, theme_get_color_idx(COLOR_BORDER));
    if (msg && msg[0])
        cjk_text(4, y0, msg, color, bg_title);
}

void gui_button(int x, int y, int w, const char* text, int focused) {
    int h = 16 * THEME_SF;
    uint8_t bg = focused ? theme_get_color_idx(COLOR_HOVER) : theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t fg = focused ? theme_get_color_idx(COLOR_FG) : theme_get_color_idx(COLOR_FG_SOFT);
    vga_fill_round_rect(x, y, x + w, y + h, RADIUS_CTRL, bg);
    vga_draw_round_rect(x, y, x + w, y + h, RADIUS_CTRL, focused ? theme_get_color_idx(COLOR_ACCENT) : theme_get_color_idx(COLOR_BORDER));
    int tw = cjk_text_w(text);
    cjk_text(x + (w - tw) / 2, y + THEME_SF, text, fg, bg);
}

// 主按钮 (modern_ui): 低饱和强调色底, 聚焦时白色焦点环, 明度单调递增
static void gui_button_primary(int x, int y, int w, const char* text, int focused) {
    int h = 16 * THEME_SF;
    uint8_t bg = focused ? theme_get_color_idx(COLOR_ACCENT) : theme_get_color_idx(COLOR_ACCENT_SOFT);
    vga_fill_round_rect(x, y, x + w, y + h, RADIUS_CTRL, bg);
    vga_draw_round_rect(x, y, x + w, y + h, RADIUS_CTRL, focused ? theme_get_color_idx(COLOR_FG) : theme_get_color_idx(COLOR_BORDER_FOCUS));
    int tw = cjk_text_w(text);
    cjk_text(x + (w - tw) / 2, y + THEME_SF, text, theme_get_color_idx(COLOR_FG), bg);
}

void gui_textbox(int x, int y, int w, const char* text, int len, int focused) {
    int h = 14;
    uint8_t field_bg = theme_get_color_idx(COLOR_FIELD);
    uint8_t fg = theme_get_color_idx(COLOR_FG);
    vga_fill_round_rect(x, y, x + w, y + h, RADIUS_CTRL, field_bg);
    vga_draw_round_rect(x, y, x + w, y + h, RADIUS_CTRL, focused ? theme_get_color_idx(COLOR_BORDER_FOCUS) : theme_get_color_idx(COLOR_BORDER));
    // 内容可能超宽, 截取尾部
    int avail = (w - 6) / 8;
    if (len > avail) text = text + (len - avail);
    vga_draw_text(x + 3, y + 3, text, fg, field_bg);
    if (focused) {
        int cx = x + 3 + (len < avail ? len : avail) * 8;
        vga_fill_rect(cx, y + 2, cx + 5, y + h - 2, fg);
    }
}

// ---- 通用表单对话框 ----
int gui_dialog_form(const char* title, DialogField* fields, int nfields,
                    const char* btn_ok, const char* btn_cancel) {
    if (nfields > DIALOG_FIELDS_MAX) nfields = DIALOG_FIELDS_MAX;
    int total = nfields + 2;      // 焦点总数 (字段 + 2 按钮)
    int focus = 0;
    int pw = 210, ph = 18 + nfields * 22 + 8 + 30 + 6;
    int px0 = (VGA_W - pw) / 2, py0 = (SCREEN_H - ph) / 2;
    int px1 = px0 + pw, py1 = py0 + ph;

    uint8_t panel_bg = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t title_bg = theme_get_color_idx(COLOR_BG_TITLE);
    uint8_t fg_title = theme_get_color_idx(COLOR_FG_TITLE);
    uint8_t fg_soft = theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t border_c = theme_get_color_idx(COLOR_BORDER);

    for (;;) {
        // 绘制面板 (ui_polish: 圆角 RADIUS_PANEL)
        vga_fill_round_rect(px0, py0, px1, py1, RADIUS_PANEL, panel_bg);
        vga_draw_round_rect(px0, py0, px1, py1, RADIUS_PANEL, border_c);
        vga_fill_rect(px0, py0, px1, py0 + 15, title_bg);
        int tw = cjk_text_w(title);
        cjk_text(px0 + (pw - tw) / 2, py0, title, fg_title, title_bg);

        // 字段
        for (int i = 0; i < nfields; i++) {
            int fy = py0 + 22 + i * 22;
            cjk_text(px0 + 10, fy + 2, fields[i].label, fg_soft, panel_bg);
            char display[128];
            int l = *fields[i].len;
            if (fields[i].secret) {
                for (int j = 0; j < l && j < 120; j++) display[j] = '*';
                display[l < 120 ? l : 120] = '\0';
            } else {
                for (int j = 0; j < l && j < 120; j++) display[j] = fields[i].buf[j];
                display[l < 120 ? l : 120] = '\0';
            }
            gui_textbox(px0 + 76, fy, pw - 86, display, l, focus == i);
        }

        // 按钮 (modern_ui: 主按钮强调色, 次按钮中性色)
        int by = py1 - 30;
        int bw = 70;
        gui_button_primary(px0 + 20, by, bw, btn_ok, focus == nfields);
        gui_button(px0 + 20 + bw + 30, by, bw, btn_cancel, focus == nfields + 1);

        int k = kb_wait();

        if (k == KEY_ESC) return 0;

        if (focus < nfields) {
            if (k == KEY_TAB || k == KEY_DOWN) {
                focus = (focus + 1) % total;
            } else if (k == KEY_UP) {
                focus = (focus + total - 1) % total;
            } else if (k == KEY_ENTER) {
                return 1;
            } else if (k == KEY_BS) {
                int* l = fields[focus].len;
                if (*l > 0) {
                    (*l)--;
                    fields[focus].buf[*l] = '\0';
                }
            } else if (k >= 32 && k <= 126) {
                int* l = fields[focus].len;
                if (*l < fields[focus].max) {
                    fields[focus].buf[*l] = (char)k;
                    (*l)++;
                    fields[focus].buf[*l] = '\0';
                }
            }
        } else {
            if (k == KEY_ENTER || k == ' ') {
                return (focus == nfields) ? 1 : 0;
            }
            if (k == KEY_LEFT || k == KEY_RIGHT) {
                focus = (focus == nfields) ? nfields + 1 : nfields;
            } else if (k == KEY_TAB || k == KEY_DOWN) {
                focus = (focus + 1) % total;
            } else if (k == KEY_UP) {
                focus = (focus + total - 1) % total;
            }
        }
    }
}

// ---- 确认对话框 ----
int gui_dialog_confirm(const char* title, const char* msg,
                       const char* btn_yes, const char* btn_no) {
    int pw = 220, ph = 76;
    int px0 = (VGA_W - pw) / 2, py0 = (SCREEN_H - ph) / 2;
    int px1 = px0 + pw, py1 = py0 + ph;
    int focus = 0;    // 0 = yes, 1 = no

    uint8_t panel_bg = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t title_bg = theme_get_color_idx(COLOR_BG_TITLE);
    uint8_t fg_title = theme_get_color_idx(COLOR_FG_TITLE);
    uint8_t fg = theme_get_color_idx(COLOR_FG);
    uint8_t border_c = theme_get_color_idx(COLOR_BORDER);

    for (;;) {
        vga_fill_round_rect(px0, py0, px1, py1, RADIUS_PANEL, panel_bg);
        vga_draw_round_rect(px0, py0, px1, py1, RADIUS_PANEL, border_c);
        vga_fill_rect(px0, py0, px1, py0 + 15, title_bg);
        int tw = cjk_text_w(title);
        cjk_text(px0 + (pw - tw) / 2, py0, title, fg_title, title_bg);

        tw = cjk_text_w(msg);
        cjk_text(px0 + (pw - tw) / 2, py0 + 24, msg, fg, panel_bg);

        int bw = 60;
        int by = py1 - 28;
        gui_button_primary(px0 + 35, by, bw, btn_yes, focus == 0);
        gui_button(px0 + pw - 35 - bw, by, bw, btn_no, focus == 1);

        int k = kb_wait();
        if (k == KEY_ESC) return 0;
        if (k == KEY_LEFT || k == KEY_RIGHT) focus = 1 - focus;
        if (k == KEY_ENTER || k == ' ') return focus == 0 ? 1 : 0;
    }
}
