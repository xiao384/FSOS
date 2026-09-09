// paging.h - 虚拟内存 / 页表抽象层
//
// 阶段 1: 提供页表操作 API, 为阶段 2 (进程) 的独立地址空间做准备。
// 当前页表由 loader.asm 静态构建 (4 级页表, 2MB 大页), 本模块在内核启动后
// 提供动态映射 / 查询 / 地址空间创建与切换能力。
#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// 页表项标志位
#define PG_PRESENT  0x001
#define PG_RW       0x002
#define PG_USER     0x004
#define PG_PS       0x080   // 大页 (2MB, PD 级)

// 内存区域划分 (与 loader.asm 页表一致):
//   0x00000000 - 0x1FFFFFFF (0-512MB)    : supervisor (内核)
//   0x20000000 - 0xBFFFFFFF (512MB-3GB)  : user (用户空间)
//   0xC0000000 - 0xFFFFFFFF (3GB-4GB)    : supervisor (设备/MMIO)
#define KERNEL_SPACE_END   0x20000000ULL   // 512MB
#define USER_SPACE_START   0x20000000ULL   // 512MB
#define USER_SPACE_END     0xC0000000ULL   // 3GB
#define USER_STACK_TOP     0x20010000ULL   // 用户栈顶 (512MB+64KB)
#define USER_CODE_BASE     0x20020000ULL   // 用户代码加载地址 (512MB+128KB)

// 4 级页表层级
typedef enum {
    PT_L4 = 0,   // PML4
    PT_L3 = 1,   // PDPT
    PT_L2 = 2,   // PD (2MB 大页在此级)
    PT_L1 = 3,   // PT (4KB 页在此级)
} pt_level_t;

// 初始化: 读取当前 CR3, 记算页表布局
void paging_init(void);

// 获取当前 CR3 (页表基址)
uint64_t paging_get_cr3(void);

// 切换地址空间 (加载新 CR3)
void paging_switch(uint64_t cr3);

// 克隆当前页表: 分配新 PML4, 复制所有项 (浅拷贝, 共享下级页表)
// 返回新 PML4 物理地址 (CR3 值), 0=失败
uint64_t paging_clone_kernel(void);

// 映射一个 4KB 页: vaddr -> paddr, flags 为 PG_* 组合
// 自动创建中间页表 (PML4/PDPT/PD), 使用内核堆分配
int paging_map_4k(uint64_t vaddr, uint64_t paddr, uint64_t flags);

// 映射一个 2MB 大页: vaddr -> paddr, flags 为 PG_* 组合
int paging_map_2m(uint64_t vaddr, uint64_t paddr, uint64_t flags);

// 查询虚拟地址的映射: 返回物理地址, *flags 存页表项标志, 未映射返回 -1
int64_t paging_query(uint64_t vaddr, uint64_t* flags);

// 判断地址是否在用户空间
static inline int paging_is_user(uint64_t vaddr) {
    return vaddr >= USER_SPACE_START && vaddr < USER_SPACE_END;
}

// 判断地址是否在内核空间
static inline int paging_is_kernel(uint64_t vaddr) {
    return vaddr < KERNEL_SPACE_END;
}

#endif // PAGING_H