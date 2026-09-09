// kheap.h - 内核堆分配器接口
#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

// 堆起点: MicroPython GC 堆结束于 0x01800000, 内核堆紧随其后
#define KHEAP_START 0x01800000UL
// 堆上限: 扩展至 0x1C000000 (~448MB), 但严格低于用户代码区(0x20000000),
// 以免与用户进程代码 / 内核栈(保留 0x1C000000..0x1D000000)冲突。
#define KHEAP_LIMIT 0x1C000000UL

void  kheap_init(void);
void* kmalloc(size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t size);

// 诊断
size_t kheap_used(void);
size_t kheap_free_total(void);

#endif // KHEAP_H
