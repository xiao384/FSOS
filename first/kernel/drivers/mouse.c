// mouse.c - PS/2 鼠标驱动 (中断驱动)
//
// 架构要点:
//   键盘与鼠标共用 i8042 数据端口 0x60, 只有 0x64 状态寄存器 bit5(aux)能区分
//   字节归属. 过去 kb/mouse 两个 ISR 都"盲读" 0x60, 一旦键鼠事件交织就会互相
//   偷字节 → 鼠标包错位 → 指针朝固定错误方向移动/漂移. 因此这里提供
//   ps2_drain_all(): 任一 PS/2 IRQ 触发时把输出缓冲清空, 按 aux 位把字节正确
//   分派给鼠标状态机 / 键盘处理函数, 从根上杜绝串扰.
//
//   鼠标 3 字节包 [flag, dx, dy] 用滑窗状态机对齐: flag 的 bit3 恒为 1, 在等待
//   flag 时丢弃任何 bit3=0 的字节, 因此单次丢字节引起的错位最多造成一个垃圾包
//   即自愈, 不会像"整窗丢弃"那样长期乱序.
//
//   位移为 9 位二补数(低 8 位 + flag 的 bit4/bit5 作符号扩展).
//   坐标钳制到 320x200 (mode13h); 屏幕 y 向下.
//
// 手感/防漂移:
//   mode13h 逻辑屏仅 320x200, GOP 下按整数倍放大(如 3x). 若按 1 计数=1 逻辑
//   像素累加, 指针可见速度会是宿主光标的 scale 倍 → 不跟手/过冲. 用亚像素残差
//   把增量折算 SENS_FP/256(85/256≈1/3, 对 3x 放大视觉 1:1), 慢速移动不丢步.
//   单包位移超过 MAX_STEP 视为误同步/异常, 丢弃.

#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "gfx.h"    // gfx_width()/gfx_height() 动态分辨率 (LFB 640x480 等)


#define PS2_STAT 0x64
#define PS2_DATA 0x60
#define OBUF_BIT 0x01        // 0x64 bit0: 输出缓冲满
#define AUX_BIT  0x20        // 0x64 bit5: 输出字节属于鼠标(aux)

#define SENS_FP 85           // 灵敏度定点8位(分母256): 85 ≈ 1/3 (逻辑像素/宿主计数)
#define MAX_STEP 40          // 单包位移上限: 超过视为误同步/异常, 丢弃
#define Y_DOWN_POSITIVE 0    // 1 = PS/2 dy 正值表示向下(与屏幕同向); 0 = dy 正值表示向上
#define TRACE_EVERY 256      // 每 N 个有效包经 mouse_get 输出一行调试到 COM1

static volatile int     g_x = 0, g_y = 0;   // mouse_init 设为屏幕居中
static volatile int     g_dx = 0, g_dy = 0;
static volatile uint8_t g_btn = 0;     // bit0=左, bit1=右, bit2=中
static volatile uint8_t g_present = 0;

// 亚像素残差(单位 1/256 像素)
static volatile int g_fx = 0, g_fy = 0;

// ---- 3 字节包同步状态机 ----
static volatile uint8_t g_flg = 0;
static volatile uint8_t g_rx = 0, g_ry = 0;
static volatile int     g_st = 0;      // 0=等flag 1=等dx 2=等dy

// ---- 调试 trace ----
static volatile uint32_t g_pkt_cnt = 0;
static volatile int      g_trace_pending = 0;
static volatile int      g_tr_f, g_tr_dx, g_tr_dy, g_tr_x, g_tr_y;

extern void kb_dispatch_byte(uint8_t b);   // kb.c: 处理一个键盘扫描码

// ---- i8042 等待 ----
static void kbc_wait_wr(void) {
    for (int i = 0; i < 100000; i++) if (!(inb(PS2_STAT) & 0x02)) return;
}
static void kbc_wait_rd(void) {
    for (int i = 0; i < 100000; i++) if (inb(PS2_STAT) & 0x01) return;
}
static void kbc_cmd(uint8_t c)  { kbc_wait_wr(); outb(PS2_STAT, c); }
static void kbc_data(uint8_t d) { kbc_wait_wr(); outb(PS2_DATA, d); }
static uint8_t kbc_read(void)   { kbc_wait_rd(); return inb(PS2_DATA); }

// 经 0xD4 前缀向鼠标发命令, 并等待 ACK(0xFA)
static void mouse_cmd(uint8_t c) {
    kbc_cmd(0xD4);
    kbc_data(c);
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STAT) & 0x01) {
            if (inb(PS2_DATA) == 0xFA) return;
        }
    }
}

// ---- 鼠标包应用: 灵敏度 + 残差 + 钳位 ----
static void mouse_apply(int dx, int dy) {
    if (dx > MAX_STEP || dx < -MAX_STEP) dx = 0;
    if (dy > MAX_STEP || dy < -MAX_STEP) dy = 0;

    g_fx += dx * SENS_FP;
    int sx = g_fx / 256;                  // C 除法向零截断
    g_fx -= sx * 256;                     // 保留残差, 慢速移动不丢步
    g_fy += dy * SENS_FP;
    int sy = g_fy / 256;
    g_fy -= sy * 256;

    if (sx != 0 || sy != 0) {
        g_x += sx;
        if (Y_DOWN_POSITIVE) g_y += sy; else g_y -= sy;   // 屏幕 y 取反
        if (g_x < 0) g_x = 0; else if (g_x >= gfx_width()) g_x = gfx_width() - 1;
        if (g_y < 0) g_y = 0; else if (g_y >= gfx_height()) g_y = gfx_height() - 1;
        g_dx += sx;
        g_dy += sy;                       // 语义: 屏幕坐标系位移(与显示方向一致)
    }
}

// 收到一个鼠标原始字节(可能来自 IRQ12 或 ps2_drain_all)
static void mouse_feed(uint8_t b) {
    switch (g_st) {
    case 0:                              // 等 flag
        if (b & 0x08) { g_flg = b; g_st = 1; }
        /* else: 错位/噪声字节, 丢弃, 继续等 */
        break;
    case 1:                              // 等 dx
        g_rx = b; g_st = 2;
        break;
    default: {                           // 等 dy → 组包完成
        g_ry = b; g_st = 0;
        uint8_t f = g_flg;
        int dx = (f & 0x10) ? (int)g_rx - 256 : (int)g_rx;   // 9 位符号扩展
        int dy = (f & 0x20) ? (int)g_ry - 256 : (int)g_ry;
        mouse_apply(dx, dy);
        g_btn = (uint8_t)(f & 7);

        // 调试 trace: 每 TRACE_EVERY 个有效包记录一行, 在 mouse_get 中非中断输出
        if (++g_pkt_cnt % TRACE_EVERY == 0) {
            g_tr_f = f; g_tr_dx = dx; g_tr_dy = dy;
            g_tr_x = g_x; g_tr_y = g_y;
            g_trace_pending = 1;
        }
        break;
    }                                   // default: 等 dy → 组包完成
    }                                   // switch (g_st)
}

// 键盘/鼠标 IRQ 共用: 清空 0x60 输出缓冲并按归属分派字节
void ps2_drain_all(void) {
    for (;;) {
        uint8_t st = inb(PS2_STAT);
        if (!(st & OBUF_BIT)) break;
        uint8_t b = inb(PS2_DATA);
        if (st & AUX_BIT) mouse_feed(b);
        else              kb_dispatch_byte(b);
    }
}

static void mouse_irq(void) {
    ps2_drain_all();
}

// ---- COM1 调试输出(供 mouse_get 调用, 避免在中断里阻塞) ----
static void trace_ch(char c) {
    for (volatile int w = 0; w < 20000; w++) {      // 等待 THRE
        if (inb(0x3F8 + 5) & 0x20) break;
    }
    outb(0x3F8, (uint8_t)c);
}
static void trace_out_dec(int v, int pad) {
    char t[8]; int n = 0, i;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    if (neg) t[n++] = '-';
    for (i = n; i < pad; i++) trace_ch(' ');
    while (n > 0) trace_ch(t[--n]);
}
static void trace_emit(void) {
    trace_ch('M'); trace_ch(' ');                   // mouse trace 前缀
    trace_out_dec(g_tr_f, 3);
    trace_ch(' '); trace_out_dec(g_tr_dx, 5);
    trace_ch(' '); trace_out_dec(g_tr_dy, 5);
    trace_ch(' '); trace_out_dec(g_tr_x, 4);
    trace_ch(' '); trace_out_dec(g_tr_y, 4);
    trace_ch('\r'); trace_ch('\n');
    g_trace_pending = 0;
}

void mouse_init(void) {
    g_x = gfx_width() / 2; g_y = gfx_height() / 2;
    g_dx = 0; g_dy = 0;
    g_fx = 0; g_fy = 0;
    g_st = 0; g_pkt_cnt = 0; g_trace_pending = 0;
    // 读配置字节, 使能鼠标 IRQ12
    kbc_cmd(0x20);
    uint8_t cfg = kbc_read();
    cfg |= 0x02;        // 使能 IRQ12
    cfg &= ~0x20;       // 不禁用鼠标时钟
    kbc_cmd(0x60);
    kbc_data(cfg);

    mouse_cmd(0xF4);    // 开启鼠标数据流

    // 鼠标在从片(PIC2)上, 需先解屏蔽主片级联 IRQ2
    irq_enable(2);
    irq_register(12, mouse_irq);
    irq_enable(12);

    g_present = 1;
}

void mouse_get(mouse_state_t* out) {
    if (g_trace_pending) trace_emit();      // 非中断上下文输出调试行
    out->x = g_x; out->y = g_y;
    out->dx = g_dx; out->dy = g_dy;
    out->left   = (g_btn & 1);
    out->right  = (g_btn >> 1) & 1;
    out->middle = (g_btn >> 2) & 1;
    out->present = g_present;
    g_dx = 0; g_dy = 0;
}
