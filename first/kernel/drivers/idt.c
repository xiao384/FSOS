// idt.c - IDT / PIC / PIT 实现 (32/64 位通用, 64 位下用 16 字节门)
#include "idt.h"
#include "io.h"
#include "gfx.h"    // gfx_gop_present(): legacy mode13h compatibility presenter
#include "sched.h"  // 阶段 2: 时钟中断驱动调度
#include <stdint.h>

// 外部 ISR 桩 (由 idt_asm.asm 提供)
extern void isr0(void);   extern void isr1(void);   extern void isr2(void);   extern void isr3(void);
extern void isr4(void);   extern void isr5(void);   extern void isr6(void);   extern void isr7(void);
extern void isr8(void);   extern void isr9(void);   extern void isr10(void);  extern void isr11(void);
extern void isr12(void);  extern void isr13(void);  extern void isr14(void);  extern void isr15(void);
extern void isr16(void);  extern void isr17(void);  extern void isr18(void);  extern void isr19(void);
extern void isr20(void);  extern void isr21(void);  extern void isr22(void);  extern void isr23(void);
extern void isr24(void);  extern void isr25(void);  extern void isr26(void);  extern void isr27(void);
extern void isr28(void);  extern void isr29(void);  extern void isr30(void);  extern void isr31(void);
extern void irq0(void);   extern void irq1(void);   extern void irq2(void);   extern void irq3(void);
extern void irq4(void);   extern void irq5(void);   extern void irq6(void);   extern void irq7(void);
extern void irq8(void);   extern void irq9(void);   extern void irq10(void);  extern void irq11(void);
extern void irq12(void);  extern void irq13(void);  extern void irq14(void);  extern void irq15(void);

// 64 位 IDT 门 (16 字节) —— 也兼容 32 位编译 (offset_high 用 0)
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_gate_t;

typedef struct {
    uint16_t limit;
    uintptr_t base;      // 32 位下 4 字节, 64 位下 8 字节
} __attribute__((packed)) idtr_t;

static idt_gate_t g_idt[256];
static idtr_t     g_idtr;
static void (*g_irq_handlers[16])(void) = {0};
static volatile uint32_t g_ticks = 0;

static void idt_set_gate(uint8_t n, uintptr_t handler, uint16_t sel, uint8_t type) {
    g_idt[n].offset_low  = (uint16_t)(handler & 0xFFFF);
    g_idt[n].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    g_idt[n].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    g_idt[n].selector    = sel;
    g_idt[n].ist         = 0;
    g_idt[n].type_attr   = 0x80 | type;  // P=1, DPL=0
    g_idt[n].reserved    = 0;
}

static void set_isr(uint8_t n, void (*handler)(void)) {
    idt_set_gate(n, (uintptr_t)handler, 0x08, IDT_GATE_INTERRUPT);
}

static void set_irq(uint8_t n, void (*handler)(void)) {
    set_isr(n, handler);
}

void idt_init(void) {
    // CPU 异常 0-31
    set_isr(0,  isr0);   set_isr(1,  isr1);   set_isr(2,  isr2);   set_isr(3,  isr3);
    set_isr(4,  isr4);   set_isr(5,  isr5);   set_isr(6,  isr6);   set_isr(7,  isr7);
    set_isr(8,  isr8);   set_isr(9,  isr9);   set_isr(10, isr10);  set_isr(11, isr11);
    set_isr(12, isr12);  set_isr(13, isr13);  set_isr(14, isr14);  set_isr(15, isr15);
    set_isr(16, isr16);  set_isr(17, isr17);  set_isr(18, isr18);  set_isr(19, isr19);
    set_isr(20, isr20);  set_isr(21, isr21);  set_isr(22, isr22);  set_isr(23, isr23);
    set_isr(24, isr24);  set_isr(25, isr25);  set_isr(26, isr26);  set_isr(27, isr27);
    set_isr(28, isr28);  set_isr(29, isr29);  set_isr(30, isr30);  set_isr(31, isr31);

    // IRQ 0-15 重映射到 32-47
    set_irq(32, irq0);   set_irq(33, irq1);   set_irq(34, irq2);   set_irq(35, irq3);
    set_irq(36, irq4);   set_irq(37, irq5);   set_irq(38, irq6);   set_irq(39, irq7);
    set_irq(40, irq8);   set_irq(41, irq9);   set_irq(42, irq10);  set_irq(43, irq11);
    set_irq(44, irq12);  set_irq(45, irq13);  set_irq(46, irq14);  set_irq(47, irq15);

    g_idtr.limit = sizeof(g_idt) - 1;
    g_idtr.base  = (uintptr_t)&g_idt;

    __asm__ volatile("lidt %0" :: "m"(g_idtr));
}

void pic_init(void) {
    // ICW1: 开始初始化，级联
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    // ICW2: 重映射向量基址
    outb(PIC1_DATA, IRQ0_VEC);
    outb(PIC2_DATA, IRQ8_VEC);

    // ICW3: 级联连接
    outb(PIC1_DATA, 0x04);  // IRQ2 连接从片
    outb(PIC2_DATA, 0x02);  // 从片连接到主片 IRQ2

    // ICW4: 8086 模式
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // OCW1: 先屏蔽所有中断
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void irq_enable(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  mask = inb(port);
    mask &= ~(1 << (irq & 7));
    outb(port, mask);
}

void irq_disable(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  mask = inb(port);
    mask |= (1 << (irq & 7));
    outb(port, mask);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void irq_register(uint8_t irq, void (*handler)(void)) {
    if (irq < 16) g_irq_handlers[irq] = handler;
}

static void exception_dump(uint32_t vector, uint32_t eip, uint32_t cs, uint32_t eflags);

// 由汇编 ISR/IRQ 桩调用的通用处理函数
// 参数: vector, eip, cs, eflags (由 idt_asm.asm 的 isr_common 压入)
void idt_dispatch(uint32_t vector, uint32_t eip, uint32_t cs, uint32_t eflags) {
    if (vector >= IRQ0_VEC && vector <= IRQ15_VEC) {
        uint8_t irq = (uint8_t)(vector - IRQ0_VEC);
        if (g_irq_handlers[irq]) g_irq_handlers[irq]();
        pic_send_eoi(irq);
    } else if (vector < 32) {
        // CPU 异常: 打印寄存器现场并停机, 避免静默忽略后无限重执行
        exception_dump(vector, eip, cs, eflags);
        for (;;) { __asm__ volatile("cli; hlt"); }
    }
}

static const char* const EXC_NAMES[32] = {
    "divide error", "debug", "NMI", "breakpoint",
    "overflow", "BOUND", "invalid opcode", "device not available",
    "double fault", "coprocessor overrun", "invalid TSS", "segment not present",
    "stack-segment fault", "general protection", "page fault", "reserved",
    "x87 FPU error", "alignment check", "machine check", "SIMD exception",
    "virtualization", "control-protection", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved"
};

static void exc_putc(char c) {
    while (!(inb(0x3FD) & 0x20)) { }
    outb(0x3F8, (uint8_t)c);
}
static void exc_puts(const char* s) {
    for (; *s; ++s) exc_putc(*s);
}
static void exc_puthex(uint32_t v) {
    const char* d = "0123456789ABCDEF";
    exc_putc('0'); exc_putc('x');
    for (int i = 28; i >= 0; i -= 4) exc_putc(d[(v >> i) & 0xF]);
}

// 由 idt_asm.asm 的 isr_common 填充: #PF 故障线性地址
uint64_t g_exc_cr2 = 0;

static void exc_puthex64(uint64_t v) {
    const char* d = "0123456789ABCDEF";
    exc_putc('0'); exc_putc('x');
    for (int i = 60; i >= 0; i -= 4) exc_putc(d[(v >> i) & 0xF]);
}

// 异常现场打印 (串口 + VGA 屏幕), 然后停机
static void exception_dump(uint32_t vector, uint32_t eip, uint32_t cs, uint32_t eflags) {
    extern void vga_draw_text(int x, int y, const char* s, uint8_t fg, uint8_t bg);
    extern void vga_fill_rect(int x0, int y0, int x1, int y1, uint8_t c);
    extern void vga_clear(uint8_t color);
    const char* name = (vector < 32) ? EXC_NAMES[vector] : "exception";

    // 串口
    exc_puts("*** EXCEPTION #");
    exc_puthex(vector);
    exc_puts(": ");
    exc_puts(name);
    exc_puts(" at eip=");
    exc_puthex(eip);
    exc_puts(" cs=");
    exc_puthex(cs);
    exc_puts(" eflags=");
    exc_puthex(eflags);
    exc_puts(" cr2=");
    exc_puthex64(g_exc_cr2);
    exc_puts("\r\n");

    // VGA 屏幕
    vga_clear(0);   // 黑底
    vga_fill_rect(0, 0, 319, 199, 1);  // 深蓝背景
    vga_draw_text(20, 40, "KERNEL EXCEPTION", 15, 1);
    vga_draw_text(20, 60, name, 10, 1);           // 绿色异常名
    char buf[32];
    buf[0] = '#'; buf[1] = '0' + (vector / 10) % 10; buf[2] = '0' + (vector % 10); buf[3] = '\0';
    vga_draw_text(20, 76, buf, 15, 1);
    char eipbuf[16];
    eipbuf[0] = 'E'; eipbuf[1] = 'I'; eipbuf[2] = 'P'; eipbuf[3] = ':'; eipbuf[4] = '\0';
    vga_draw_text(20, 92, eipbuf, 15, 1);
    const char* hexd = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[9 - i] = hexd[(eip >> (4 * i)) & 0xF];
    buf[10] = '\0';
    vga_draw_text(52, 92, buf, 15, 1);
    vga_draw_text(20, 120, "System halted.", 15, 1);
    (void)eflags;
}

// PIT 定时器中断：每 tick 1ms
static void pit_handler(void) {
    g_ticks++;
    // UEFI 下屏幕只由 GOP 帧缓冲驱动: 周期把 0xA0000 前端镜像上屏
    // (内部有 ~40ms 节流; BIOS 路径无 GOP 信息时自动失效)
    gfx_gop_present();
    // 阶段 2: 通知调度器检查是否需要切换线程
    sched_request();
}

void pit_init(uint32_t hz) {
    g_ticks = 0;
    uint32_t divisor = PIT_FREQUENCY / hz;
    outb(PIT_CMD, 0x36);  // 通道 0，读写低/高字节，模式 3
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, pit_handler);
    irq_enable(0);
}

uint32_t get_ticks(void) {
    return g_ticks;
}

// ---- 空闲 / CPU 利用率统计 ----
uint64_t g_idle_ticks = 0;

uint64_t get_idle_ticks(void) {
    return g_idle_ticks;
}

// 低功耗空闲一拍: 开中断后 hlt, 唤醒后计入空闲统计。
// 前台应用延迟循环里用它代替忙等, 使空闲时间正确计入 CPU 利用率分母。
void cpu_idle_halt(void) {
    __asm__ volatile("sti; hlt");
    g_idle_ticks++;
}

void sleep_ms(uint32_t ms) {
    uint32_t end = g_ticks + ms;
    while (g_ticks < end) {
        __asm__ volatile("sti; hlt");
    }
}
