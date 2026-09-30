// cjk.h - 中英混排文本绘制 (8bpp 后台缓冲)
// ASCII 用引导器装入的 8x8 VGA 字体; 常规汉字用 16x16，Shell/UI 使用 24x24 源字库
// (user/cjk_font.h, 由 tools/gen_cjk_font.py 生成)。
#ifndef CJK_H
#define CJK_H

#include <stdint.h>

// 中文字符格宽/高 (像素); ASCII 字符宽 8 像素, 高 8 像素
#define CJK_CHAR_W   16
#define CJK_CHAR_H   16

// 建议的行距 (汉字 16 高 + 2px 行间隙); 供表格/列表/正文排版复用
#define CJK_LINE_H   18
#define CJK_UI_CHAR_W 16
#define CJK_UI_CHAR_H 16
#define CJK_UI_ASCII_W 8
#define CJK_UI_ASCII_H 16

// 测量字符串的像素宽度 (UTF-8: ASCII 8px, 汉字 16px)
int  cjk_text_w(const char* s);

// 在后台缓冲绘制 UTF-8 字符串。fg/bg 为调色板索引。
// ASCII 字形高 8px (垂直居于一格内 +4px); 汉字占 16x16。
void cjk_text(int x, int y, const char* s, uint8_t fg, uint8_t bg);

// 截断版: 若字符串超过 max_w 像素则按字符截断并追加 ".."
int  cjk_text_ellipsis(int x, int y, const char* s, int max_w,
                       uint8_t fg, uint8_t bg);

// 现代 Shell/UI 文本：使用经过真实 CJK 字体栅格化的 16x16 字形，
// 由 gfx 原生缩放到设备像素；中文与 ASCII 使用统一的行高与基线。
int  cjk_ui_text_w(const char* s);
void cjk_ui_text(int x, int y, const char* s, uint8_t fg, uint8_t bg);
// 透明背景版: 不铺背景格, 字形混合到现有帧 (玻璃顶栏/壁纸标签用)
void cjk_ui_text_over(int x, int y, const char* s, uint8_t fg);
int  cjk_ui_text_ellipsis_over(int x, int y, const char* s, int max_w, uint8_t fg);
int  cjk_ui_text_ellipsis(int x, int y, const char* s, int max_w,
                          uint8_t fg, uint8_t bg);

// 单个字符是否 CJK (用于文本内缩进/取列)
int  cjk_is_cjk(uint32_t cp);

#endif // CJK_H
