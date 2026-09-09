// kb.h - PS/2 键盘轮询驱动 (扫描码集 1)
#ifndef KB_H
#define KB_H

#include <stdint.h>

// 扩展按键编码 (>0xFF 以区别于 ASCII)
#define KEY_UP    0x1100
#define KEY_DOWN  0x1101
#define KEY_LEFT  0x1102
#define KEY_RIGHT 0x1103
#define KEY_HOME  0x1104
#define KEY_END   0x1105
#define KEY_PGUP  0x1106
#define KEY_PGDN  0x1107
#define KEY_INS   0x1108
#define KEY_DEL   0x1109

// 功能键 (扫描码集 1)
#define KEY_F1   0x1301
#define KEY_F2   0x1302
#define KEY_F3   0x1303
#define KEY_F4   0x1304
#define KEY_F5   0x1305
#define KEY_F6   0x1306
#define KEY_F7   0x1307
#define KEY_F8   0x1308
#define KEY_F9   0x1309
#define KEY_F10  0x130A
#define KEY_F11  0x130B
#define KEY_F12  0x130C
// Win / Super 键单独按下产生的事件
#define KEY_WIN  0x1401

#define KEY_ESC  27
#define KEY_ENTER '\r'
#define KEY_BS    '\b'
#define KEY_TAB   '\t'

// 修饰键状态掩码 (见 kb_mods)
#define KB_MOD_SHIFT 0x1
#define KB_MOD_CTRL  0x2
#define KB_MOD_ALT   0x4
#define KB_MOD_GUI   0x8   // Win / Super 键

// 返回当前修饰键状态 (KB_MOD_* 位掩码)
int kb_mods(void);

void kb_init(void);
// 处理一个键盘扫描码(由 ps2_drain_all 分派调用)
void kb_dispatch_byte(uint8_t sc);
// 轮询键盘, 返回 0 表示无按键; 否则返回 ASCII 或 KEY_* 扩展码
int kb_poll(void);
// 阻塞等待一个按键
int kb_wait(void);

#endif // KB_H
