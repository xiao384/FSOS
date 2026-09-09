// kheap.c - 内核堆分配器 (边界标签隐式空闲链表, 首次适配 + 合并)
//
// 设计:
//   - 每个块: [header:8][payload][footer:8], 块大小 16 字节对齐。
//   - header 低 1 位为 ALLOC 标志, 其余为块总长(含 header+footer)。
//   - footer 镜像 header, 用于前向/后向合并。
//   - sbrk 向上扩展, 上限 KHEAP_LIMIT。
//   物理地址 == 虚拟地址 (内核前 1GB 恒等映射), 直接返回物理指针即可。
#include "kheap.h"
#include <stdint.h>

#define WSIZE 8
#define DSIZE 16
#define ALIGN_UP(x, a)  (((x) + (a) - 1) & ~((size_t)(a) - 1))
#define CHUNK_WORDS  (4096 / WSIZE)   // 每次扩展 4KB

#define PACK(size, alloc)  ((size) | (alloc))
#define GET(p)    (*(volatile size_t*)(p))
#define PUT(p, v) (*(volatile size_t*)(p) = (v))

#define GET_SIZE(p)  (GET(p) & ~0xFUL)
#define GET_ALLOC(p) (GET(p) & 0x1UL)

// bp = 块指针(指向 payload)
#define HDRP(bp)  ((size_t*)(bp) - 1)
#define FTRP(bp)  ((size_t*)(bp) + (GET_SIZE(HDRP(bp)) / WSIZE) - 2)
#define NEXT_BLKP(bp)  ((char*)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp)  ((char*)(bp) - GET_SIZE((size_t*)(bp) - 2))

static char*  g_heap_base;   // 第一个块 (prologue 之后)
static char*  g_brk;         // 当前堆顶 (epilogue 所在)
static size_t g_used;        // 已分配字节 (诊断)

static void* extend_heap(size_t words) {
    size_t w = (words % 2) ? words + 1 : words;   // 偶数 word, 保持对齐
    if ((size_t)g_brk + w * WSIZE > KHEAP_LIMIT) return NULL;
    char* old = g_brk;
    // 新块: header + footer(epilogue 旧的被覆盖, 末尾写新 epilogue)
    PUT(HDRP(old), PACK(w * WSIZE, 0));      // 新空闲块 header
    PUT(FTRP(old), PACK(w * WSIZE, 0));      // 新空闲块 footer
    g_brk = old + w * WSIZE;
    PUT(HDRP(g_brk), PACK(0, 1));            // 新 epilogue
    PUT(FTRP(g_brk), PACK(0, 1));
    // 与之前可能的空闲块合并
    return (void*)old;   // 返回该空闲块 payload
}

void kheap_init(void) {
    g_heap_base = (char*)KHEAP_START;
    g_brk = g_heap_base;
    g_used = 0;
    // prologue: 分配 dummy 块, 使 PREV_BLKP 安全
    PUT(g_heap_base, PACK(DSIZE * 2, 1));        // prologue header
    PUT(g_heap_base + DSIZE, PACK(DSIZE * 2, 1)); // prologue footer
    g_brk = g_heap_base + DSIZE * 2;
    PUT(HDRP(g_brk), PACK(0, 1));                 // epilogue
    PUT(FTRP(g_brk), PACK(0, 1));
    extend_heap(CHUNK_WORDS);
}

static void* coalesce(void* bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) return bp;
    if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

static void* find_fit(size_t asize) {
    // 首次适配, 从 prologue 之后的第一个块开始
    for (char* p = g_heap_base + DSIZE * 2; ; p = NEXT_BLKP(p)) {
        size_t h = GET(HDRP(p));
        if ((h & ~0xFUL) == 0) break;            // 到达 epilogue
        if (!(h & 0x1) && (h & ~0xFUL) >= asize) return p;
    }
    return NULL;
}

static void place(void* bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    if (csize - asize >= DSIZE * 4) {            // 剩余足够拆出新空闲块
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        char* nb = (char*)NEXT_BLKP(bp);
        PUT(HDRP(nb), PACK(csize - asize, 0));
        PUT(FTRP(nb), PACK(csize - asize, 0));
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size_t asize = ALIGN_UP(size + DSIZE * 2, DSIZE * 2); // header+footer + 对齐
    void* bp = find_fit(asize);
    if (!bp) {
        bp = extend_heap(asize / WSIZE + CHUNK_WORDS);
        if (!bp) return NULL;
        bp = coalesce(bp);
    }
    place(bp, asize);
    g_used += asize;
    return bp;
}

void kfree(void* ptr) {
    if (!ptr) return;
    PUT(HDRP(ptr), PACK(GET_SIZE(HDRP(ptr)), 0));
    PUT(FTRP(ptr), PACK(GET_SIZE(HDRP(ptr)), 0));
    g_used -= GET_SIZE(HDRP(ptr));
    coalesce(ptr);
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }
    size_t old = GET_SIZE(HDRP(ptr));
    size_t need = ALIGN_UP(size + DSIZE * 2, DSIZE * 2);
    if (old >= need) return ptr;
    void* np = kmalloc(size);
    if (!np) return NULL;
    // 拷贝旧内容 (取较小长度)
    size_t copy = old > need ? need : old;
    for (size_t i = 0; i < copy - DSIZE * 2; i++)
        ((char*)np)[i] = ((char*)ptr)[i];
    kfree(ptr);
    return np;
}

size_t kheap_used(void)       { return g_used; }
size_t kheap_free_total(void) {
    size_t total = 0;
    for (char* p = g_heap_base + DSIZE * 2; ; p = NEXT_BLKP(p)) {
        size_t h = GET(HDRP(p));
        if ((h & ~0xFUL) == 0) break;
        if (!(h & 0x1)) total += (h & ~0xFUL);
    }
    return total;
}
