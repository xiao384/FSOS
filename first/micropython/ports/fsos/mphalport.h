// mphalport.h - FSOS 端口 HAL 声明
// py/mphal.h 会包含本文件; 实现在 mphalport.c
#ifndef MICROPY_PORT_MPHALPORT_H
#define MICROPY_PORT_MPHALPORT_H

#include <stdint.h>
#include <stddef.h>   // size_t (py/mphal.h 在包含本文件前未定义 size_t)

// 控制台复位(清屏, 进入 Python 前调用)
void mp_hal_console_reset(void);

// 标准输出
uintptr_t mp_hal_stdout_tx_strn(const char *str, uintptr_t len);

// 标准输入(阻塞读一个字符)
int mp_hal_stdin_rx_chr(void);

// 设置中断字符 (REPL 的 Ctrl+C)
void mp_hal_set_interrupt_char(int c);

// 定时器 (PIT, 1000 Hz)
uintptr_t mp_hal_ticks_ms(void);
uintptr_t mp_hal_ticks_us(void);
void mp_hal_delay_ms(uintptr_t ms);
void mp_hal_delay_us(uintptr_t us);

#endif // MICROPY_PORT_MPHALPORT_H
