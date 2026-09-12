// paging.c - 虚拟内存 / 页表抽象层实现
//
// 4 级页表: PML4 -> PDPT -> PD -> PT
// 每级 512 项, 每项 8 字节, 页表大小 4KB
// 2MB 大页在 PD 级, 4KB 页在 PT 级
#include <stdint.h>
#include "paging.h"
#include "kheap.h"    // kmalloc/kfree
#include "io.h"       // 串口调试

// 页表项: 64 位
// [0] P, [1] R/W, [2] U/S, [6] D, [7] PS, [8] G, [12:51] 物理地址, [63] NX
typedef uint64_t pte_t;

// 当前页表基址 (CR3)
static uint64_t s_cr3 = 0;

// 串口调试
static void ser_putc(char c) {
    while ((inb(0x3FD) & 0x20) == 0) {}
    outb(0x3F8, (uint8_t)c);
}
static void ser_puts(const char* s) {
    for (; *s; ++s) ser_putc(*s);
}

void paging_init(void) {
    __asm__ volatile("mov %%cr3, %0" : "=r"(s_cr3));
    ser_puts("paging_init: CR3=0x");
    // 简单 hex 输出
    uint64_t v = s_cr3;
    for (int i = 60; i >= 0; i -= 4) {
        uint64_t d = (v >> i) & 0xF;
        ser_putc(d < 10 ? '0' + (char)d : 'A' + (char)(d - 10));
    }
    ser_puts("\r\n");
}

uint64_t paging_get_cr3(void) {
    __asm__ volatile("mov %%cr3, %0" : "=r"(s_cr3));
    return s_cr3;
}

void paging_switch(uint64_t cr3) {
    s_cr3 = cr3;
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3));
}

uint64_t paging_clone_kernel(void) {
    uint64_t old_cr3 = paging_get_cr3();
    uint64_t* old_pml4 = (uint64_t*)(uintptr_t)(old_cr3 & ~0xFFFULL);
    // 多分配一页, 手动 4KB 对齐 (kmalloc 不保证 4KB 对齐)
    uint64_t raw = (uint64_t)(uintptr_t)kmalloc(8192);
    if (!raw) return 0;
    uint64_t new_pml4_va = (raw + 0xFFF) & ~0xFFFULL;
    uint64_t* new_pml4 = (uint64_t*)(uintptr_t)new_pml4_va;
    for (int i = 0; i < 512; i++) new_pml4[i] = old_pml4[i];
    return new_pml4_va;  // 直接映射区, va == pa, 已 4KB 对齐
}

// 获取页表项指针, 自动创建中间级页表 (用 kmalloc 分配 4KB)
// level: 要获取哪一级的表, vaddr 对应的索引
static pte_t* walk_pt(uint64_t vaddr, pt_level_t target_level, int create) {
    uint64_t* table = (uint64_t*)(uintptr_t)(s_cr3 & ~0xFFFULL);  // PML4 基址
    int shifts[] = {39, 30, 21, 12};  // 各级索引的位移

    for (int lvl = 0; lvl < (int)target_level; lvl++) {
        int idx = (int)((vaddr >> shifts[lvl]) & 0x1FF);
        pte_t entry = table[idx];
        if (!(entry & PG_PRESENT)) {
            if (!create) return 0;
            // 分配新页表 (4KB, 物理地址 = 虚拟地址, 当前全映射)
            // kmalloc 仅保证 16 字节对齐, 而页表必须 4KB 对齐, 否则写入 CR3
            // 的基址会偏移到错误的内存页 -> 新建中间页表时页故障/错映射。
            // 多分配一页并在块内向上对齐 (与 paging_clone_kernel 一致)。
            uint64_t raw = (uint64_t)(uintptr_t)kmalloc(4096 + 0x1000);
            if (!raw) return 0;
            uint64_t new_table_va = (raw + 0xFFF) & ~0xFFFULL;
            // 清零
            uint64_t* p = (uint64_t*)(uintptr_t)new_table_va;
            for (int i = 0; i < 512; i++) p[i] = 0;
            // 设置表项: present + rw + user + 物理地址
            table[idx] = new_table_va | PG_PRESENT | PG_RW | PG_USER;
            entry = table[idx];
        }
        table = (uint64_t*)(uintptr_t)(entry & ~0xFFFULL);
    }
    int idx = (int)((vaddr >> shifts[(int)target_level]) & 0x1FF);
    return (pte_t*)(uintptr_t)&table[idx];
}

int paging_map_4k(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (vaddr & 0xFFF) return -1;  // 必须 4KB 对齐
    if (paddr & 0xFFF) return -1;
    pte_t* entry = walk_pt(vaddr, PT_L1, 1);
    if (!entry) return -1;
    *entry = (paddr & ~0xFFFULL) | flags | PG_PRESENT;
    return 0;
}

int paging_map_2m(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (vaddr & 0x1FFFFF) return -1;  // 必须 2MB 对齐
    if (paddr & 0x1FFFFF) return -1;
    pte_t* entry = walk_pt(vaddr, PT_L2, 1);
    if (!entry) return -1;
    *entry = (paddr & ~0xFFFULL) | flags | PG_PRESENT | PG_PS;
    return 0;
}

int64_t paging_query(uint64_t vaddr, uint64_t* flags) {
    uint64_t* table = (uint64_t*)(uintptr_t)(s_cr3 & ~0xFFFULL);
    int shifts[] = {39, 30, 21, 12};

    for (int lvl = 0; lvl < 3; lvl++) {
        int idx = (int)((vaddr >> shifts[lvl]) & 0x1FF);
        pte_t entry = table[idx];
        if (!(entry & PG_PRESENT)) return -1;
        if (entry & PG_PS) {
            // 大页: 命中层级不同页大小不同 (lvl=1 -> 1GB, lvl=2 -> 2MB)
            // 必须用对应层级的页内偏移掩码, 否则 1GB 页会算错物理地址。
            if (flags) *flags = entry & 0xFFF;
            uint64_t off = (1ULL << shifts[lvl]) - 1;  // 该级页大小 - 1
            uint64_t paddr = entry & ~off;
            paddr += vaddr & off;
            return (int64_t)paddr;
        }
        table = (uint64_t*)(uintptr_t)(entry & ~0xFFFULL);
    }
    int idx = (int)((vaddr >> shifts[3]) & 0x1FF);
    pte_t entry = table[idx];
    if (!(entry & PG_PRESENT)) return -1;
    if (flags) *flags = entry & 0xFFF;
    uint64_t paddr = entry & ~0xFFFULL;
    paddr += vaddr & 0xFFF;
    return (int64_t)paddr;
}