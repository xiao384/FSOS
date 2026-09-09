// mphalport.c - FSOS 上的 MicroPython HAL
// 将 MicroPython 的标准输入输出桥接到本项目内核的 串口 + VGA + 键盘.
#include <stdint.h>
#include <stdbool.h>

#include "py/mpconfig.h"
#include "py/mphal.h"
#include "py/gc.h"

#include "vga.h"      // 内核 VGA 文本原语
#include "kb.h"       // 内核键盘
#include "idt.h"      // get_ticks()
#include "io.h"       // inb/outb (串口访问)

// ============================================================
// 简单 VGA 文本控制台 (320x200, 8x8 字体 => 40x25)
// ============================================================
#define CONSOLE_COLS 40
#define CONSOLE_ROWS 25

static int g_col = 0;
static int g_row = 0;
static bool g_console_inited = false;

static void console_reset(void) {
    g_col = 0; g_row = 0;
    g_console_inited = true;
}

// 供 mp_entry.c 在进入 Python 前复位控制台光标
void mp_hal_console_reset(void) {
    console_reset();
}

// ---- 最小 VT100 输出解析 ----
// shared/readline 的行重绘会输出 ESC[K (清到行尾) / ESC[<n>D (光标左移) /
// ESC[<n>C (光标右移); 不解析的话这些序列会以 "[K" 等乱码形式直接打到屏幕上。
static int g_esc   = 0;   // 0=普通字符 1=已收到 ESC 2=已收到 ESC[
static int g_esc_n = 0;   // ESC[ <n> 累积的数值

static void console_putc(char c) {
    if (!g_console_inited) console_reset();

    if (g_esc == 1) {
        if (c == '[') { g_esc = 2; g_esc_n = 0; }
        else { g_esc = 0; }               // 其它 ESC x 序列本内核不产生, 丢弃
        return;
    }
    if (g_esc == 2) {
        if (c >= '0' && c <= '9') { g_esc_n = g_esc_n * 10 + (c - '0'); return; }
        if (c == 'K') {                   // 清除光标到行尾
            for (int x = g_col; x < CONSOLE_COLS; x++) {
                char sp[2] = " ";
                vga_draw_text(x * 8, 4 + g_row * 8, sp, COL_LGREEN, COL_BLACK);
            }
        } else if (c == 'D') {            // 光标左移 n 列
            g_col -= g_esc_n;
            if (g_col < 0) g_col = 0;
        } else if (c == 'C') {            // 光标右移 n 列
            g_col += g_esc_n;
            if (g_col > CONSOLE_COLS) g_col = CONSOLE_COLS;
        }
        // 'A'/'B'/'J'/'H' 等: 本端口输出侧不产生, 忽略
        g_esc = 0;
        return;
    }
    if ((unsigned char)c == 27) { g_esc = 1; return; }

    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        // readline 用 \r + 整行重绘, 必须先清除当前行再回行首, 否则字符叠加
        g_col = 0;
        char sp[2] = " ";
        for (int x = 0; x < CONSOLE_COLS; x++) {
            vga_draw_text(x * 8, 4 + g_row * 8, sp, COL_LGREEN, COL_BLACK);
        }
    } else if (c == 8) {  // Backspace
        if (g_col > 0) {
            g_col--;
            char sp[2] = " ";
            vga_draw_text(g_col * 8, 4 + g_row * 8, sp, COL_LGREEN, COL_BLACK);
        }
    } else if (c == '\t') {
        g_col = (g_col + 4) & ~3;
    } else {
        if (g_col >= CONSOLE_COLS) {
            g_col = 0; g_row++;
        }
        char buf[2] = { c, '\0' };
        vga_draw_text(g_col * 8, 4 + g_row * 8, buf, COL_LGREEN, COL_BLACK);
        g_col++;
    }
    if (g_row >= CONSOLE_ROWS) {
        g_row = 0; // 满屏后回到顶部 (无平滑滚动, 教学内核够用)
    }
}

// ============================================================
// 串口 COM1 (0x3F8) 输出, 与内核调试输出一致
// 注意: 串口是 x86 I/O 端口, 必须用 in/out 指令访问, 不能用内存指针!
// ============================================================
static void com1_putc(char c) {
    // 等待 THR 空 (LSR bit 5)
    while ((inb(0x3FD) & 0x20) == 0) { }
    outb(0x3F8, (uint8_t)c);
}

// 供 mp_entry.c 调试使用 (非 static)
void com1_puts(const char *s) {
    while (*s) com1_putc(*s++);
}

// 十六进制输出 (调试用)
void com1_puthex(uint32_t v) {
    const char *d = "0123456789ABCDEF";
    com1_putc('0'); com1_putc('x');
    for (int i = 28; i >= 0; i -= 4) com1_putc(d[(v >> i) & 0xF]);
}

// ============================================================
// MicroPython HAL 接口
// ============================================================
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        com1_putc(c);
        console_putc(c);
    }
    return len;
}

// ---- 扩展键 -> VT100/控制码 的待发队列 (一次按键对应多个字符) ----
static char g_pend[8];
static int  g_np = 0;
static int  g_cp = 0;

static void pend_push(const char *s) {
    while (*s && g_np < (int)sizeof(g_pend)) g_pend[g_np++] = *s++;
}

// 阻塞读取一个字符 (供 REPL / input() 使用)
int mp_hal_stdin_rx_chr(void) {
    if (g_cp < g_np) return g_pend[g_cp++];
    g_np = g_cp = 0;
    for (;;) {
        int k = kb_wait();          // 内核阻塞读键 (无键时 hlt)
        // 注意: readline (shared/readline) 用 '\r' 作为行结束, 不能转成 '\n'!
        if (k == 13) return '\r';   // Enter
        if (k == 8)  return 8;      // Backspace
        if (k == 9)  return 9;      // Tab
        if (k >= 1 && k <= 26) return k;   // 控制字符 (Ctrl+A..Z, REPL 用 Ctrl+D=4 退出)
        if (k >= 32 && k <= 126) return (char)k;
        // 扩展键 -> readline 认识的编码
        switch (k) {
            case KEY_UP:    pend_push("\x1b[A"); break;   // 上方向键: 历史上一条
            case KEY_DOWN:  pend_push("\x1b[B"); break;   // 下方向键: 历史下一条
            case KEY_RIGHT: pend_push("\x1b[C"); break;   // 光标右移
            case KEY_LEFT:  pend_push("\x1b[D"); break;   // 光标左移
            case KEY_DEL:   return 127;                   // Delete: readline 按
                                                          // 退格语义删除光标前一字符
            case KEY_HOME:  return 1;                     // Ctrl-A: 跳到行首
            case KEY_END:   return 5;                     // Ctrl-E: 跳到行尾
            default: break;                               // 其余扩展键忽略
        }
        if (g_np) return g_pend[g_cp++];
    }
}

// REPL 的 Ctrl+C 中断字符: 本端口由键盘驱动直接翻译, 无独立中断通道, 空实现.
void mp_hal_set_interrupt_char(int c) {
    (void)c;
}

// 毫秒计时 (PIT 1000Hz -> get_ticks() 即毫秒)
mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)get_ticks();
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)get_ticks() * 1000u;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    uint32_t start = get_ticks();
    while (get_ticks() - start < ms) {
        __asm__ volatile("nop");
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    uint32_t start = get_ticks();
    uint32_t target_ms = (us + 999) / 1000;
    while (get_ticks() - start < target_ms) {
        __asm__ volatile("nop");
    }
}
