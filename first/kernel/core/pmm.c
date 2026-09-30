// pmm.c - 物理页帧分配器实现 (bump 式)
#include "pmm.h"
#include "kheap.h"
#include <stdint.h>

static uint64_t s_cur = PMM_BASE;
static uint64_t s_end = PMM_END;

// 真实物理内存总量 (由引导器 e820 检测, kernel_main 写入; 0=未知)
static uint64_t s_phys_total = 0;
// 内核镜像占用 (链接脚本 __kernel_end - 1MB 加载基址)
extern char __kernel_end;
static uint64_t s_kernel_bytes = (uint64_t)&__kernel_end - 0x100000ULL;

void pmm_init(void) {
    s_cur = PMM_BASE;
    s_end = PMM_END;
}

uint64_t pmm_alloc_frame(void) {
    if (s_cur + PMM_PAGE > s_end) return 0;
    uint64_t pa = s_cur;
    s_cur += PMM_PAGE;
    return pa;
}

void pmm_free_frame(uint64_t pa) {
    (void)pa;   // bump 分配器不回收
}

void pmm_set_phys_total(uint64_t total) {
    s_phys_total = total;
}

uint64_t pmm_allocated_bytes(void) {
    return s_cur - PMM_BASE;
}

uint64_t phys_mem_total_bytes(void) {
    // 检测失败 (0) 时回退到 QEMU 默认 256MB, 保证控制中心始终有值
    return s_phys_total ? s_phys_total : (256ULL * 1024 * 1024);
}

uint64_t phys_mem_used_bytes(void) {
    uint64_t used = s_kernel_bytes;                 // 内核镜像 (代码/数据/BSS)
    used += (uint64_t)kheap_used();                 // 内核堆分配
    used += s_cur - PMM_BASE;                       // 已分配物理帧 (用户进程)
    return used;
}
