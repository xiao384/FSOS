// cxx_runtime.cpp - C++ 运行时支撑 (freestanding, 无异常/RTTI)
//
// 提供:
//   - memset/memcpy/memmove (内核未自带, C++ 聚合拷贝会生成调用)
//   - operator new/delete (接内核堆 kmalloc/kfree)
//   - __cxa_pure_virtual / __cxa_atexit 等桩
//   - ___chkstk_ms 栈探测桩 (MinGW 大栈帧时调用)
#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "kheap.h"
}

// ---- 基础内存例程 ----
// 标记为 weak: 链接 MicroPython (libfsos.a 的 string0.o 提供强符号 memset/memcpy/
// memmove) 时, 强符号优先, 避免 multiple definition; 不构建 Python 时这些 weak
// 版本即成为唯一实现, 供 C++ 聚合拷贝等使用。
extern "C" __attribute__((weak)) void* memset(void* dst, int v, size_t n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)v;
    return dst;
}
extern "C" __attribute__((weak)) void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
extern "C" __attribute__((weak)) void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { const unsigned char* ss = s + n; unsigned char* dd = d + n; while (n--) *--dd = *--ss; }
    return dst;
}

// ---- new / delete ----
extern "C" void* operator new(size_t sz)        { return kmalloc(sz ? sz : 1); }
extern "C" void* operator new[](size_t sz)      { return kmalloc(sz ? sz : 1); }
extern "C" void  operator delete(void* p)       { kfree(p); }
extern "C" void  operator delete[](void* p)     { kfree(p); }
extern "C" void  operator delete(void* p, size_t) { kfree(p); }
extern "C" void  operator delete[](void* p, size_t) { kfree(p); }

// ---- C++ ABI 桩 ----
extern "C" void __cxa_pure_virtual(void) { for (;;) ; }   // 纯虚函数误调用 -> 停机
extern "C" int  __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
extern "C" void __cxa_finalize(void*) { }
void* __dso_handle = 0;

// MinGW 大栈帧探测桩 (保持栈可访问, 这里直接返回即可,
// 因为引导已映射好前 1GB 且栈向下增长无需实际 touch)
extern "C" void ___chkstk_ms(void) { }
extern "C" void __chkstk(void)     { }
