// kb.c - PS/2 键盘驱动 (中断驱动, 扫描码集 1)
#include "kb.h"
#include "io.h"
#include "idt.h"   // g_idle_ticks
#include "idt.h"
#include "gfx.h"   // gfx_flip() (kb_wait 入口刷新画面)

// ---- 扫描码集 1 映射表 (make code -> ASCII, 无 Shift) ----
static const char scan_unshifted[128] = {
    0,   27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\r', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scan_shifted[128] = {
    0,   27,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// 扩展扫描码 (0xE0 前缀) -> KEY_* 编码
static int translate_extended(uint8_t code) {
    switch (code) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x49: return KEY_PGUP;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INS;
        case 0x53: return KEY_DEL;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;
    }
    return 0;
}

static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int gui_pressed = 0;

// ---- 按键环形缓冲 (中断写入, 用户读取) ----
#define KB_BUF_SIZE 64
static volatile int  g_kb_buf[KB_BUF_SIZE];
static volatile int  g_kb_head = 0;
static volatile int  g_kb_tail = 0;

static int buf_get(void) {
    if (g_kb_head == g_kb_tail) return 0;
    int k = g_kb_buf[g_kb_tail];
    g_kb_tail = (g_kb_tail + 1) % KB_BUF_SIZE;
    return k;
}

static void buf_put(int k) {
    int next = (g_kb_head + 1) % KB_BUF_SIZE;
    if (next != g_kb_tail) {
        g_kb_buf[g_kb_head] = k;
        g_kb_head = next;
    }
}

// 将扫描码翻译成按键编码
static int translate(uint8_t sc, int extended) {
    if (sc == 0xE0) return 0;  // 前缀不应单独到达这里

    uint8_t code = sc & 0x7F;
    int is_break = (sc & 0x80) != 0;

    // Shift 跟踪 (0x2A LShift, 0x36 RShift)
    if (code == 0x2A || code == 0x36) {
        shift_pressed = !is_break;
        return 0;
    }
    // Ctrl 跟踪 (0x1D)
    if (code == 0x1D) {
        ctrl_pressed = !is_break;
        return 0;
    }
    // Alt 跟踪 (0x38 LAlt / 0xE0 0x38 RAlt)
    if (code == 0x38) {
        alt_pressed = !is_break;
        return 0;
    }
    // GUI / Win 键 (0xE0 0x5B / 0xE0 0x5D)
    if (extended && (code == 0x5B || code == 0x5D)) {
        gui_pressed = !is_break;
        if (!is_break) return KEY_WIN;   // Win 单独按下 -> 产生事件
        return 0;
    }

    if (is_break) return 0;

    // Ctrl+字母 -> 控制字符 (仅当无 Alt/GUI, 以保留 Ctrl+Alt+Del 等组合)
    if (ctrl_pressed && !alt_pressed && !gui_pressed) {
        char ch = (code < 128) ? scan_unshifted[code] : 0;
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 1;
        return 0;
    }

    if (extended) {
        return translate_extended(code);
    }

    // 功能键 F1..F10 (扫描码集 1, 无 0xE0 前缀)
    switch (code) {
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        default: break;
    }

    // 小键盘方向键 (NumLock off 时无 0xE0 前缀)
    switch (code) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x49: return KEY_PGUP;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INS;
        case 0x53: return KEY_DEL;
        default: break;
    }

    if (code < 128) {
        return shift_pressed ? scan_shifted[code] : scan_unshifted[code];
    }
    return 0;
}

static volatile int g_kb_extended = 0;

// 处理一个键盘扫描码(由 ps2_drain_all 分派, 保证只收到真正的键盘字节)
void kb_dispatch_byte(uint8_t sc) {
    if (sc == 0xE0) {
        g_kb_extended = 1;
        return;
    }

    int k = translate(sc, g_kb_extended);
    g_kb_extended = 0;
    if (k != 0) buf_put(k);
}

// IRQ1 处理函数: 与鼠标共用输出缓冲, 必须经分派器读取,
// 否则会偷走/错读鼠标字节导致包错位(指针方向乱).
extern void ps2_drain_all(void);
static void kb_irq_handler(void) {
    ps2_drain_all();
}

int kb_mods(void) {
    int m = 0;
    if (shift_pressed) m |= KB_MOD_SHIFT;
    if (ctrl_pressed)  m |= KB_MOD_CTRL;
    if (alt_pressed)   m |= KB_MOD_ALT;
    if (gui_pressed)   m |= KB_MOD_GUI;
    return m;
}

void kb_init(void) {
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    gui_pressed = 0;
    g_kb_head = 0;
    g_kb_tail = 0;
    g_kb_extended = 0;

    // 清空控制器输出缓冲
    while (inb(0x64) & 0x01) inb(0x60);

    // 注册 IRQ1 并启用
    irq_register(1, kb_irq_handler);
    irq_enable(1);
}

// 轮询键盘, 返回 0 表示无按键; 否则返回 ASCII 或 KEY_* 扩展码
int kb_poll(void) {
    return buf_get();
}

// 阻塞等待一个按键 (中断驱动, 无按键时停机)
int kb_wait(void) {
    gfx_flip();            // 刷新画面: UI draw 完成后把后台缓冲翻到前台
    int k;
    while ((k = buf_get()) == 0) {
        // 先开中断再 hlt, 防止调用方在 cli 状态下永久睡眠
        __asm__ volatile("sti; hlt");
        g_idle_ticks++;   // 计入空闲统计 (供 CPU 利用率估算)
    }
    return k;
}
