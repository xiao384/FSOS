// idt.h - 中断描述符表 (IDT) 与 PIC/PIT 声明
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// IDT 门类型
#define IDT_GATE_INTERRUPT  0x0E
#define IDT_GATE_TRAP       0x0F
#define IDT_RING0           0x00

// PIC 端口
#define PIC1_CMD            0x20
#define PIC1_DATA           0x21
#define PIC2_CMD            0xA0
#define PIC2_DATA           0xA1
#define PIC_EOI             0x20

// IRQ 重映射后的向量号
#define IRQ0_VEC            32
#define IRQ1_VEC            33
#define IRQ2_VEC            34
#define IRQ3_VEC            35
#define IRQ4_VEC            36
#define IRQ5_VEC            37
#define IRQ6_VEC            38
#define IRQ7_VEC            39
#define IRQ8_VEC            40
#define IRQ9_VEC            41
#define IRQ10_VEC           42
#define IRQ11_VEC           43
#define IRQ12_VEC           44
#define IRQ13_VEC           45
#define IRQ14_VEC           46
#define IRQ15_VEC           47

// PIT 配置
#define PIT_CMD             0x43
#define PIT_CH0             0x40
#define PIT_FREQUENCY       1193182
#define PIT_HZ              1000  // 1ms 一次中断

// 初始化
void idt_init(void);
void pic_init(void);
void pit_init(uint32_t hz);

// 中断使能/屏蔽
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);

// 注册中断处理函数
void irq_register(uint8_t irq, void (*handler)(void));

// 发送 EOI
void pic_send_eoi(uint8_t irq);

// 获取系统滴答 (ms)
uint32_t get_ticks(void);
void sleep_ms(uint32_t ms);

// ---- 空闲/CPU 利用率统计 ----
// g_idle_ticks: 系统处于 hlt 空闲态累计的 tick 数 (由 cpu_idle_halt/kb_wait 累加)
extern uint64_t g_idle_ticks;
// 读取空闲 tick 计数
uint64_t get_idle_ticks(void);
// 进入低功耗空闲一拍 (hlt) 并计入空闲统计 —— 供前台应用延迟循环使用,
// 使空闲时间被正确计入 CPU 利用率分母
void cpu_idle_halt(void);

#endif
