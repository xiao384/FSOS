// pmm.c - 物理页帧分配器实现 (bump 式)
#include "pmm.h"
#include <stdint.h>

static uint64_t s_cur = PMM_BASE;
static uint64_t s_end = PMM_END;

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
