// gdt.c - 全局描述符表 + TSS 初始化
//
// 当前内核只有一个 64 位 GDT (boot/loader.asm 的 gdt64, 仅内核段)。
// 要支持用户态程序, 必须:
//   1) 加入 ring3 代码/数据段;
//   2) 加入 TSS (长模式), 提供 rsp0 —— 从用户态经中断/syscall 返回内核时
//      CPU 依据它切换到内核栈。
// 本模块在 kernel_main 早期调用 gdt_init(), 重建 GDT 并 lgdt/ltr。
// 长模式下数据段 limit/G 位被硬件忽略, 编码与 loader 保持一致即可。
#include <stdint.h>
#include "gdt.h"

// 内核栈顶 (kernel/core/start.asm 导出, 64KB BSS 栈)
extern char stack_top[];

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

// 长模式 TSS 结构 (Intel 手册)
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;         // ring0 栈指针 (中断/syscall 从用户态进入时使用)
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];       // 中断栈表
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss_t;

// GDT: [0]=null, [1]=内核代码, [2]=内核数据, [3]=用户数据, [4]=用户代码, [5][6]=TSS
static uint64_t g_gdt[7] __attribute__((aligned(16)));
static tss_t    g_tss;
static gdtr_t   g_gdtr;

void gdt_init(void) {
    uint64_t kstack_top = (uint64_t)(uintptr_t)stack_top;

    // 段描述符 (64 位: base/limit 大多为 0; 代码段 L=1)
    g_gdt[0] = 0;
    g_gdt[1] = 0x00209A0000000000ULL;   // 0x08 内核代码 ring0, read, L=1
    g_gdt[2] = 0x0000920000000000ULL;   // 0x10 内核数据 ring0, rw
    g_gdt[3] = 0x0000F20000000000ULL;   // 0x18 用户数据 ring3, rw
    g_gdt[4] = 0x0020FA0000000000ULL;   // 0x20 用户代码 ring3, read, L=1
    g_gdt[5] = 0;
    g_gdt[6] = 0;

    // TSS 清零并填入 rsp0
    for (int i = 0; i < (int)sizeof(tss_t); i++) ((uint8_t*)&g_tss)[i] = 0;
    g_tss.rsp0 = kstack_top;

    // TSS 系统描述符 (16 字节, base 64 位)
    uint64_t base  = (uint64_t)(uintptr_t)&g_tss;
    uint32_t limit = (uint32_t)sizeof(tss_t) - 1;
    g_gdt[5] = (limit & 0xFFFF) |
               ((base & 0xFFFFFFULL) << 16) |
               (0x89ULL << 40) |
               (((uint64_t)(limit >> 16) & 0xF) << 48) |
               (((base >> 24) & 0xFFULL) << 56);
    g_gdt[6] = (base >> 32) & 0xFFFFFFFFULL;

    g_gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdtr.base  = (uint64_t)(uintptr_t)&g_gdt;

    // 加载 GDTR 与 TR
    __asm__ volatile("lgdt %0" :: "m"(g_gdtr) : "memory");
    __asm__ volatile("ltr %0"  :: "a"((uint16_t)GDT_TSS_SEL) : "memory");
}

void gdt_set_rsp0(uint64_t rsp) {
    g_tss.rsp0 = rsp;
}