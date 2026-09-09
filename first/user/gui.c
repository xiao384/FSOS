// gui.c - 图形界面组件与对话框框架
// 屏幕 320x200, 8x8 字符, 键盘导航
#include "gui.h"
#include "kb.h"

void gui_title_bar(const char* left, const char* right) {
    vga_fill_rect(0, 0, VGA_W - 1, 13, COL_TITLEBG);
    vga_draw_rect(0, 0, VGA_W - 1, 13, COL_LBLUE);
    vga_draw_text(4, 3, left, COL_WHITE, COL_TITLEBG);
    if (right && right[0]) {
        int w = vga_text_w(right);
        vga_draw_text(VGA_W - 4 - w, 3, right, COL_LGRAY, COL_TITLEBG);
    }
}

void gui_status_bar(const char* msg, uint8_t color) {
    int y0 = SCREEN_H - 14, y1 = SCREEN_H - 1;
    vga_fill_rect(0, y0, VGA_W - 1, y1, COL_TITLEBG);
    vga_draw_rect(0, y0, VGA_W - 1, y1, COL_LBLUE);
    if (msg && msg[0])
        vga_draw_text(4, y0 + 4, msg, color, COL_TITLEBG);
}

void gui_button(int x, int y, int w, const char* text, int focused) {
    int h = 16;
    uint8_t bg = focused ? COL_PANEL_HI : COL_PANEL;
    uint8_t fg = focused ? COL_WHITE : COL_LGRAY;
    vga_fill_rect(x, y, x + w, y + h, bg);
    vga_draw_rect(x, y, x + w, y + h, focused ? COL_LBLUE : COL_DGRAY);
    int tw = vga_text_w(text);
    vga_draw_text(x + (w - tw) / 2, y + 4, text, fg, bg);
}

void gui_textbox(int x, int y, int w, const char* text, int len, int focused) {
    int h = 14;
    vga_fill_rect(x, y, x + w, y + h, COL_FIELD);
    vga_draw_rect(x, y, x + w, y + h, focused ? COL_WHITE : COL_DGRAY);
    // 内容可能超宽, 截取尾部
    int avail = (w - 6) / 8;
    if (len > avail) text = text + (len - avail);
    vga_draw_text(x + 3, y + 3, text, COL_WHITE, COL_FIELD);
    if (focused) {
        int cx = x + 3 + (len < avail ? len : avail) * 8;
        vga_fill_rect(cx, y + 2, cx + 5, y + h - 2, COL_WHITE);
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

    for (;;) {
        // 绘制面板
        vga_fill_rect(px0, py0, px1, py1, COL_PANEL);
        vga_draw_rect(px0, py0, px1, py1, COL_LBLUE);
        vga_fill_rect(px0, py0, px1, py0 + 14, COL_TITLEBG);
        int tw = vga_text_w(title);
        vga_draw_text(px0 + (pw - tw) / 2, py0 + 3, title, COL_WHITE, COL_TITLEBG);

        // 字段
        for (int i = 0; i < nfields; i++) {
            int fy = py0 + 22 + i * 22;
            vga_draw_text(px0 + 10, fy + 3, fields[i].label, COL_LGRAY, COL_PANEL);
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

        // 按钮
        int by = py1 - 30;
        int bw = 70;
        gui_button(px0 + 20, by, bw, btn_ok, focus == nfields);
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

    for (;;) {
        vga_fill_rect(px0, py0, px1, py1, COL_PANEL);
        vga_draw_rect(px0, py0, px1, py1, COL_LBLUE);
        vga_fill_rect(px0, py0, px1, py0 + 14, COL_TITLEBG);
        int tw = vga_text_w(title);
        vga_draw_text(px0 + (pw - tw) / 2, py0 + 3, title, COL_WHITE, COL_TITLEBG);

        tw = vga_text_w(msg);
        vga_draw_text(px0 + (pw - tw) / 2, py0 + 28, msg, COL_WHITE, COL_PANEL);

        int bw = 60;
        int by = py1 - 28;
        gui_button(px0 + 35, by, bw, btn_yes, focus == 0);
        gui_button(px0 + pw - 35 - bw, by, bw, btn_no, focus == 1);

        int k = kb_wait();
        if (k == KEY_ESC) return 0;
        if (k == KEY_LEFT || k == KEY_RIGHT) focus = 1 - focus;
        if (k == KEY_ENTER || k == ' ') return focus == 0 ? 1 : 0;
    }
}
