// mouse.h - PS/2 鼠标驱动 (中断驱动, 默认 3 字节包)
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

typedef struct {
    int x, y;            // 已钳制到屏幕的坐标
    int dx, dy;          // 自上次 mouse_get 的累计位移
    uint8_t left, right, middle;
    uint8_t present;     // 初始化是否成功 (1=已启用)
} mouse_state_t;

// 初始化 i8042 + 鼠标, 注册 IRQ12 并使能
void mouse_init(void);
// 读取当前状态, 并清零累计位移 dx/dy
void mouse_get(mouse_state_t* out);
// 键盘/鼠标 IRQ 共用: 清空 0x60 缓冲并按 0x64.bit5(aux) 分派字节到键/鼠状态机
void ps2_drain_all(void);

#endif // MOUSE_H
