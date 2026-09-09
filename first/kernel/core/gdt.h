// gdt.h - 全局描述符表 + TSS (用户态/内核态隔离的地基)
#ifndef GDT_H
#define GDT_H

// 段选择子
#define GDT_KERNEL_CS  0x08   // 内核代码段 (ring0)
#define GDT_KERNEL_DS  0x10   // 内核数据段 (ring0)
#define GDT_USER_DS    0x18   // 用户数据段 (ring3, 使用时 |3 = 0x1B)
#define GDT_USER_CS    0x20   // 用户代码段 (ring3, 使用时 |3 = 0x23)
#define GDT_TSS_SEL    0x28   // TSS 段选择子

// 建立 GDT (含用户态段) + TSS 并加载 GDTR/TR
void gdt_init(void);

// 更新 TSS.rsp0 (用户态中断/syscall 进入内核时用的栈顶)
// 调度器切换线程时调用, 使每个进程有独立的内核栈
void gdt_set_rsp0(uint64_t rsp);

#endif // GDT_H