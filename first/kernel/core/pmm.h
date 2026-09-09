// pmm.h - 物理页帧分配器 (bump 式, 供 Linuxulator 等分配用户进程物理内存)
//
// 内核使用 0..4GB 恒等映射, 堆严格 < 0x1C000000, 内核栈保留 0x1C000000..0x1D000000,
// 模块窗口在 1GB..3GB (按需)。因此把用户物理帧分配区放在 [0x1D000000, 0x40000000)
// 这段空闲区, 既不会踩内核/堆, 又低于模块窗口起始 (1GB)。
#ifndef PMM_H
#define PMM_H

#include <stdint.h>

// 物理帧分配区 (4KB 页)
#define PMM_BASE  0x1D000000ULL   // 464MB: 内核栈保留区之上
#define PMM_END   0x40000000ULL   // 1GB:  模块窗口 (CINT) 之下
#define PMM_PAGE  0x1000ULL

void    pmm_init(void);
// 分配一页物理内存 (4KB 对齐), 返回物理地址; 0=耗尽
uint64_t pmm_alloc_frame(void);
// 释放一页 (bump 分配器不回收, 此处仅为接口完整性保留)
void     pmm_free_frame(uint64_t pa);

#endif // PMM_H
