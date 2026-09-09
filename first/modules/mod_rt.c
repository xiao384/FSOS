// mod_rt.c - 模块公共运行时 (替代 libc, freestanding 环境必须自包含)
// 提供 mem*/str* 助手; -nostdlib 下 GCC 可能 emits 对这些符号的调用,
// 故必须在此定义。所有符号名与标准 C 库一致。
#include <stddef.h>
#include <stdint.h>

void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* dd = (unsigned char*)d;
    const unsigned char* ss = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
    return d;
}
void* memmove(void* d, const void* s, size_t n) {
    unsigned char* dd = (unsigned char*)d;
    const unsigned char* ss = (const unsigned char*)s;
    if (dd < ss) { for (size_t i = 0; i < n; i++) dd[i] = ss[i]; }
    else { for (size_t i = n; i > 0; i--) dd[i-1] = ss[i-1]; }
    return d;
}
void* memset(void* d, int v, size_t n) {
    unsigned char* dd = (unsigned char*)d;
    for (size_t i = 0; i < n; i++) dd[i] = (unsigned char)v;
    return d;
}
int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* aa = (const unsigned char*)a;
    const unsigned char* bb = (const unsigned char*)b;
    for (size_t i = 0; i < n; i++)
        if (aa[i] != bb[i]) return (int)aa[i] - (int)bb[i];
    return 0;
}
size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (a[i] == 0) break;
    }
    return 0;
}
char* strcpy(char* d, const char* s) {
    int i = 0; while ((d[i] = s[i])) i++; return d;
}
char* strncpy(char* d, const char* s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char* strcat(char* d, const char* s) {
    int i = 0; while (d[i]) i++;
    int j = 0; while ((d[i++] = s[j++]));
    return d;
}
char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return 0;
}
