// cjk.h - 中英混排文本绘制 (8bpp 后台缓冲)
// ASCII 用引导器装入 0xB0000 的 8x8 VGA 字体; 汉字用内嵌 12x12 字库
// (user/cjk_font.h, 由 tools/gen_cjk_font.py 生成)。
#ifndef CJK_H
#define CJK_H

#include <stdint.h>

// 中文字符格宽 (像素); ASCII 字符宽 8 像素
#define CJK_CHAR_W   12
#define CJK_CHAR_H   12

// 测量字符串的像素宽度 (UTF-8: ASCII 8px, 汉字 12px)
int  cjk_text_w(const char* s);

// 在后台缓冲绘制 UTF-8 字符串。fg/bg 为调色板索引。
// ASCII 字形高 8px (垂直居于一格内 +2px); 汉字占 12x12。
void cjk_text(int x, int y, const char* s, uint8_t fg, uint8_t bg);

// 截断版: 若字符串超过 max_w 像素则按字符截断并追加 ".."
int  cjk_text_ellipsis(int x, int y, const char* s, int max_w,
                       uint8_t fg, uint8_t bg);

// 单个字符是否 CJK (用于文本内缩进/取列)
int  cjk_is_cjk(uint32_t cp);

#endif // CJK_H
