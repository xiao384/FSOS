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
#include "cjk.h"      // 中英混排文本原语 (cjk_text / cjk_is_cjk)
#include "gfx.h"      // gfx_clip_active / gfx_reset_clip

// T4.1: 缓冲输出模式 — stdout 经 console_emit 进入环形缓冲 (module.c), 不直写 VGA
extern void console_emit(char c);
static int g_stdout_buffered = 0;
void mp_hal_set_buffered(int on) { g_stdout_buffered = on ? 1 : 0; }

// ============================================================
// VGA 文本控制台: 16px 行高 + UTF-8 中英混排
// 旧版逐字节按 8x8 ASCII 字体绘制, 遇到 UTF-8 中文会把每个字节画成 '?'
// (如 banner "键入 exit / quit 或 Ctrl+D 退出" 显示成 "?????? ... ???")。
// 这里改为先按 UTF-8 解码, 再用 cjk_text 绘制: ASCII 8x8 居中, 汉字 16x16。
// ============================================================
#define CONSOLE_TOP   4
#define CONSOLE_ROWH  16                                   // 行高 16px (容纳 16x16 汉字)
#define CONSOLE_COLS  (VGA_W / 8)                          // 列以 8px 为单位 (汉字占 2 列)
#define CONSOLE_ROWS  ((SCREEN_H - CONSOLE_TOP) / CONSOLE_ROWH)

static int g_col = 0;      // 列 (8px 单位)
static int g_row = 0;
static bool g_console_inited = false;

// UTF-8 多字节累积缓冲 (序列可能跨多次 tx_strn 调用)
static char g_ub[5];
static int  g_un = 0;
static int  g_uneed = 0;

static void console_reset(void) {
    g_col = 0; g_row = 0; g_un = 0; g_uneed = 0;
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

static int console_row_y(void) { return CONSOLE_TOP + g_row * CONSOLE_ROWH; }

// 清除当前行 [col, 行尾) 的格子 (黑底)
static void console_clear_to_eol(int col) {
    if (col < 0) col = 0;
    if (col >= CONSOLE_COLS) return;
    int y = console_row_y();
    int x0 = col * 8;
    int x1 = CONSOLE_COLS * 8 - 1;
    if (x1 > VGA_W - 1) x1 = VGA_W - 1;
    if (x1 >= x0) vga_fill_rect(x0, y, x1, y + CONSOLE_ROWH - 1, COL_BLACK);
}

// 输出一个已解码的码点; bytes 为该码点的 UTF-8 字节 (以 0 结尾)
static void console_emit_cp(const char* bytes, uint32_t cp) {
    int w = cjk_is_cjk(cp) ? 2 : 1;
    if (g_col + w > CONSOLE_COLS) { g_col = 0; g_row++; }
    if (g_row >= CONSOLE_ROWS) g_row = 0;
    cjk_text(g_col * 8, console_row_y(), bytes, COL_LGREEN, COL_BLACK);
    g_col += w;
    if (g_col >= CONSOLE_COLS) { g_col = 0; g_row++; if (g_row >= CONSOLE_ROWS) g_row = 0; }
}

static void console_putc(char c) {
    if (!g_console_inited) console_reset();

    // REPL 由 WM 的窗口终端 (terminal_key -> cmd_python) 拉起, 此时 WM 合成器
    // 可能还残留着脏区重绘用的裁剪框 (gfx_set_clip)。裁剪框会丢弃 console 在
    // 绝对坐标上的绘制, 表现为 banner 完全不可见(画面停在上一帧)。这里强制清除。
    if (gfx_clip_active()) gfx_reset_clip();

    if (g_esc == 1) {                     // 已收到 ESC
        if (c == '[') { g_esc = 2; g_esc_n = 0; }   // CSI
        else if (c == ']') { g_esc = 3; }           // OSC (丢弃到 BEL / ST)
        else { g_esc = 0; }
        return;
    }
    if (g_esc == 2) {                     // CSI: 参数 0x20-0x3F, 终止字节 0x40-0x7E
        unsigned char u = (unsigned char)c;
        if (u >= 0x20 && u <= 0x3F) {
            if (u >= '0' && u <= '9') g_esc_n = g_esc_n * 10 + (u - '0');
            return;                       // '?', ';', '>' 等参数/中间字节一律吞掉
        }
        if (u >= 0x40 && u <= 0x7E) {
            if (u == 'K') {               // 清除光标到行尾
                console_clear_to_eol(g_col);
            } else if (u == 'D') {        // 光标左移 n 列
                g_col -= g_esc_n;
                if (g_col < 0) g_col = 0;
            } else if (u == 'C') {        // 光标右移 n 列
                g_col += g_esc_n;
                if (g_col > CONSOLE_COLS) g_col = CONSOLE_COLS;
            }
            // 其余 (J/H/A/B/n/?25h/?25l ...) 忽略
        }
        g_esc = 0;
        return;
    }
    if (g_esc == 3) {                     // OSC: 直到 BEL(7) 或 ESC 结束
        if ((unsigned char)c == 7 || (unsigned char)c == 27) g_esc = 0;
        return;
    }
    if ((unsigned char)c == 27) { g_esc = 1; return; }

    if (c == '\n') {
        g_col = 0;
        g_row++;
        if (g_row >= CONSOLE_ROWS) g_row = 0;
        return;
    }
    if (c == '\r') {
        // 回车 = 光标回到行首, 不清行 (标准终端语义)。
        // 注意: 早期实现会在此清掉整行, 但 REPL banner 形如
        //   "\r\nFSOS MicroPython (…)\r\n"
        // 结尾的 '\r' 会把刚打印的 banner 整行擦掉 (屏幕上只剩下一行的 ">>> ")。
        // readline 的“原地重绘”需要清行时会发 ESC[K, 已在上面的转义分支处理。
        g_col = 0;
        return;
    }
    if (c == 8) {  // Backspace
        if (g_col > 0) {
            g_col--;
            int y = console_row_y();
            vga_fill_rect(g_col * 8, y, g_col * 8 + 7, y + CONSOLE_ROWH - 1, COL_BLACK);
        }
        return;
    }
    if (c == '\t') {
        g_col = (g_col + 4) & ~3;
        if (g_col >= CONSOLE_COLS) g_col = CONSOLE_COLS - 1;
        return;
    }

    // ---- UTF-8 组装: 逐字节累积, 凑齐一个码点后再绘制 ----
    unsigned char u = (unsigned char)c;
    if (g_un == 0) {
        if (u < 0x80) { char b[2] = { (char)u, 0 }; console_emit_cp(b, u); return; }
        if      ((u & 0xE0) == 0xC0) g_uneed = 2;
        else if ((u & 0xF0) == 0xE0) g_uneed = 3;
        else if ((u & 0xF8) == 0xF0) g_uneed = 4;
        else { char b[2] = { '?', 0 }; console_emit_cp(b, '?'); return; }
        g_ub[g_un++] = (char)u;
        return;
    }
    if ((u & 0xC0) != 0x80) { g_un = 0; g_uneed = 0; return; }   // 非法续字节, 丢弃
    g_ub[g_un++] = (char)u;
    if (g_un < g_uneed) return;
    g_ub[g_un] = 0;
    uint32_t cp;
    if (g_uneed == 2)
        cp = ((uint32_t)(g_ub[0] & 0x1F) << 6) | (uint32_t)(g_ub[1] & 0x3F);
    else if (g_uneed == 3)
        cp = ((uint32_t)(g_ub[0] & 0x0F) << 12) | ((uint32_t)(g_ub[1] & 0x3F) << 6) |
             (uint32_t)(g_ub[2] & 0x3F);
    else
        cp = ((uint32_t)(g_ub[0] & 0x07) << 18) | ((uint32_t)(g_ub[1] & 0x3F) << 12) |
             ((uint32_t)(g_ub[2] & 0x3F) << 6) | (uint32_t)(g_ub[3] & 0x3F);
    console_emit_cp(g_ub, cp);
    g_un = 0; g_uneed = 0;
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
        if (g_stdout_buffered) console_emit(c);
        else console_putc(c);
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
