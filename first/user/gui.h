// gui.h - 图形界面组件与对话框框架
#ifndef GUI_H
#define GUI_H

#include "vga.h"

#define DIALOG_FIELDS_MAX 3

typedef struct {
    const char* label;
    char* buf;      // 内容缓冲 (外部提供)
    int* len;       // 当前长度
    int max;        // 最大长度
    int secret;     // 1 = 密码框 (显示 *)
} DialogField;

// 顶部标题栏
void gui_title_bar(const char* left, const char* right);
// 底部状态栏
void gui_status_bar(const char* msg, uint8_t color);
// 按钮 (聚焦时高亮)
void gui_button(int x, int y, int w, const char* text, int focused);
// 输入框 (focus 时显示光标块)
void gui_textbox(int x, int y, int w, const char* text, int len, int focused);

// 通用表单对话框 (返回 1=确定, 0=取消)
int gui_dialog_form(const char* title, DialogField* fields, int nfields,
                    const char* btn_ok, const char* btn_cancel);
// 确认对话框 (返回 1=是, 0=否)
int gui_dialog_confirm(const char* title, const char* msg,
                       const char* btn_yes, const char* btn_no);

#endif // GUI_H
