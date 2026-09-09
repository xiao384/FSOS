// pinyin.h - 拼音输入法
// 在文本输入界面中按 Ctrl+Space 切换中/英文输入模式。
// 中文模式下: 字母键累积为拼音音节, 空格/数字键选词, Backspace 删音节, Esc 取消。
#ifndef PINYIN_H
#define PINYIN_H

#include <stdint.h>

// 最大拼音音节长度 (如 "zhuang" = 6)
#define PINYIN_MAX_SYL  8
// 每音节最多候选数
#define PINYIN_MAX_CAND 10
// 输出 UTF-8 字符最大字节 (一个汉字 3 字节 + '\0')
#define PINYIN_OUT_MAX  4

// 初始化 / 重置输入状态
void pinyin_reset(void);

// 拼音模式是否开启
int  pinyin_active(void);

// 开关拼音模式 (返回新状态)
int  pinyin_toggle(void);

// 喂入一个按键 (ASCII 或 KEY_*)
//  返回值:
//    0  = 按键已消费 (正在拼音节或选词, 调用方不应处理)
//    >0 = 透传按键 (非拼音模式或非字母键, 调用方自行处理)
//    -1 = 已选出一个汉字, 用 pinyin_get_output() 取结果
int  pinyin_feed(int key);

// 当前正在输入的拼音音节 (如 "ni", 空串表示无正在输入)
const char* pinyin_get_compose(void);

// 候选列表 (UTF-8, 如 "1:你 2:尼 3:泥 ..."), 空串表示无候选
const char* pinyin_get_candidates(void);

// 取出选中的汉字 (UTF-8, 3字节+'\0'); 调用后状态自动重置音节
const char* pinyin_get_output(void);

#endif // PINYIN_H