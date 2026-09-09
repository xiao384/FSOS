// kstring.c - 内核最小 C 字符串/内存函数
//
// 背景: 内核以 -ffreestanding 编译, 不链接系统 libc。多数字符串操作被 GCC
// 内联为 __builtin_*, 但当调用点无法内联时 (例如 -Os 下的长循环或跨函数
// 常量传播失效), 编译器仍会生成对 strcmp/strcpy/strlen 的外部引用, 导致
// 链接期 "undefined reference"。此前内核代码量小、全部被内联所以没暴露;
// 接入语言运行时框架 (lang.c) 与 C 解释器 (cint.c) 后开始报未定义符号。
//
// 因此这里提供一份最小实现 (仅内核自身用到的部分)。memcpy/memset/memmove
// 已在 cxx_runtime.cpp 中以 weak 符号提供, 此处不重复定义以免冲突。
#include <stddef.h>

size_t strlen(const char* s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n--) {
        if (*a != *b) return (int)(unsigned char)*a - (int)(unsigned char)*b;
        if (!*a) break;
        a++; b++;
    }
    return 0;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* p = (const unsigned char*)a;
    const unsigned char* q = (const unsigned char*)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}
