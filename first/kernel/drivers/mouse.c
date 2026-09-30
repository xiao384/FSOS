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

#define SENS_FP 85           // legacy mode13h fallback sensitivity
#define MAX_STEP 40          // 单包位移上限: 超过视为误同步/异常, 丢弃
#define Y_DOWN_POSITIVE 0    // 1 = PS/2 dy 正值表示向下(与屏幕同向); 0 = dy 正值表示向上
#define TRACE_EVERY 256      // 每 N 个有效包经 mouse_get 输出一行调试到 COM1

static volatile int     g_x = 0, g_y = 0;   // mouse_init 设为屏幕居中
static volatile int     g_dx = 0, g_dy = 0;
static volatile int     g_wheel = 0;
static volatile uint8_t g_btn = 0;     // bit0=左, bit1=右, bit2=中
static volatile uint8_t g_present = 0;
static volatile int     g_abs = 0;     // 1 = VMware 绝对鼠标已启用(替代相对 PS/2)

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

    // GOP 原生高分辨率下，一个 PS/2 设备计数就是一个真实屏幕像素；
    // 旧的 85/256 会让 1920x1080 下鼠标慢约 3 倍。
    int sens = gfx_is_lfb() ? 256 : SENS_FP;
    g_fx += dx * sens;
    int sx = g_fx / 256;                  // C 除法向零截断
    g_fx -= sx * 256;                     // 保留残差, 慢速移动不丢步
    int ysens = gfx_is_lfb() ? 256 : SENS_FP;
    g_fy += dy * ysens;
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
    if (g_abs) return;   // 绝对模式下 PS/2 字节为噪声, 忽略; 坐标由 vmmouse_poll 提供
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

// ---- VMware vmmouse 绝对鼠标 ----
// FSOS 当前仅有相对 PS/2 鼠标. 在 VMware 中, 当客户机未启用绝对鼠标时,
// VMware 会持续合成朝角落的相对位移 (指针自动漂移). 探测到 VMware 后端后改用
// 绝对坐标上报: 之后指针位置直接从 VMware backdoor 读取 (0..0xFFFF 全屏范围),
// 不再累计相对增量, 漂移即消除. 裸金属/其它 Hypervisor 探测失败则回退相对 PS/2.
// 协议常量与序列见 Linux drivers/input/mouse/vmmouse.c.
#define VMWARE_MAGIC        0x564D5868U
#define VMWARE_BDOOR        0x5658
#define VMCMD_ABSP_DATA     39
#define VMCMD_ABSP_STATUS   40
#define VMCMD_ABSP_CMD      41
#define VMCMD_ABSP_RESTRICT 86
#define VMM_ENABLE          0x45414552U
#define VMM_REQ_ABS         0x53424152U
#define VMM_VERSION_ID      0x3442554AU
#define VMM_ERROR           0xFFFF0000U
#define VMM_BTN_LEFT        0x20
#define VMM_BTN_RIGHT       0x10
#define VMM_BTN_MIDDLE      0x08

// 单条 backdoor 命令: ecx=cmd, ebx=arg, eax=VMWARE_MAGIC, dx=0x5658; 返回 eax.
// 必须在 ring0 (内核态) 执行; FSOS 用户进程与内核同特权级, 故可直接调用.
static inline uint32_t vmware_call(uint32_t cmd, uint32_t arg, uint32_t* out_ebx) {
    uint32_t eax = VMWARE_MAGIC;
    uint32_t ebx = arg;
    uint32_t ecx = cmd;
    uint32_t edx = VMWARE_BDOOR;
    __asm__ volatile ("inl %%dx, %%eax\n"
                      : "+a"(eax), "+b"(ebx), "+c"(ecx), "+d"(edx));
    if (out_ebx) *out_ebx = ebx;
    return eax;
}

// 尝试启用 VMware 绝对鼠标; 成功返回 1.
static int vmmouse_enable(void) {
    vmware_call(VMCMD_ABSP_CMD, VMM_ENABLE, 0);
    uint32_t status = vmware_call(VMCMD_ABSP_STATUS, 0, 0);
    if ((status & 0xFFFF) == 0) return 0;          // 空 flags -> 无设备
    uint32_t ver = vmware_call(VMCMD_ABSP_DATA, 1, 0);
    if (ver != VMM_VERSION_ID) return 0;           // 版本不符 -> 非 VMware vmmouse
    vmware_call(VMCMD_ABSP_RESTRICT, 0x01 /*CPL0*/, 0);
    vmware_call(VMCMD_ABSP_CMD, VMM_REQ_ABS, 0);
    return 1;
}

// 排空 VMware 绝对坐标队列, 取最后一帧更新 g_x/g_y/g_btn (绝对模式直接定位).
static void vmmouse_poll(void) {
    int lx = -1, ly = -1, lbtn = 0;
    for (;;) {
        uint32_t status = vmware_call(VMCMD_ABSP_STATUS, 0, 0);
        if (status & VMM_ERROR) break;
        uint32_t len = status & 0xFFFF;
        if (len == 0) break;
        if (len % 4) break;                        // 无效队列长度
        // 读 4 个字: eax=status/buttons, ebx=x, ecx=y, edx=z
        uint32_t a = VMWARE_MAGIC, b = 4, c = VMCMD_ABSP_DATA, d = VMWARE_BDOOR;
        __asm__ volatile ("inl %%dx, %%eax\n"
                          : "+a"(a), "+b"(b), "+c"(c), "+d"(d));
        uint32_t st = a;
        lx = (int)(b & 0xFFFF);
        ly = (int)(c & 0xFFFF);
        // VMMouse z 为垂直滚轮增量；按有符号低 16 位解释。
        // 注意: VMware 实际在"物理上滚"时给出负 Z，而 mouse.h 约定为"正=上滚"，
        // 这里取负使 g_wheel 符号与硬件手势一致 (上滚为正)。
        int wz = (int)(d & 0xFFFF);
        if (wz & 0x8000) wz -= 0x10000;
        if (wz > -64 && wz < 64) g_wheel -= wz;
        lbtn = (uint8_t)(((st & VMM_BTN_LEFT)   ? 1 : 0)
                       | (((st & VMM_BTN_RIGHT)  ? 1 : 0) << 1)
                       | (((st & VMM_BTN_MIDDLE) ? 1 : 0) << 2));
    }
    if (lx >= 0) {
        int sw = gfx_width(), sh = gfx_height();
        if (sw <= 0 || sh <= 0) return;
        int nx = (int)(((long long)lx * (sw - 1)) / 0xFFFF);
        int ny = (int)(((long long)ly * (sh - 1)) / 0xFFFF);
        if (nx < 0) nx = 0; else if (nx >= sw) nx = sw - 1;
        if (ny < 0) ny = 0; else if (ny >= sh) ny = sh - 1;
        g_dx += nx - g_x;                          // 供需要相对位移的消费者
        g_dy += ny - g_y;
        g_x = nx; g_y = ny;
        g_btn = (uint8_t)lbtn;
    }
}

void mouse_init(void) {
    g_x = gfx_width() / 2; g_y = gfx_height() / 2;
    g_dx = 0; g_dy = 0; g_wheel = 0;
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

    // 尝试 VMware 绝对鼠标; 成功则后续 mouse_get 直接读 backdoor, 消除漂移
    if (vmmouse_enable()) {
        g_abs = 1;
        trace_ch('V'); trace_ch('M'); trace_ch('A'); trace_ch('\n');
    } else {
        trace_ch('V'); trace_ch('M'); trace_ch('D'); trace_ch('\n');
    }
}

void mouse_get(mouse_state_t* out) {
    if (g_abs) vmmouse_poll();              // VMware 绝对模式: 直接读 backdoor 坐标
    if (g_trace_pending) trace_emit();      // 非中断上下文输出调试行
    out->x = g_x; out->y = g_y;
    out->dx = g_dx; out->dy = g_dy;
    out->wheel = g_wheel;
    out->left   = (g_btn & 1);
    out->right  = (g_btn >> 1) & 1;
    out->middle = (g_btn >> 2) & 1;
    out->present = g_present;
    g_dx = 0; g_dy = 0; g_wheel = 0;
}
