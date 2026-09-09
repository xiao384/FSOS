// mingw_compat.c - MinGW 标准库符号兼容层
//
// MinGW 的 <stdio.h> 通过 __MINGW_ASM_CALL 把 snprintf/vsnprintf 重定向为
// __mingw_snprintf/__mingw_vsnprintf (链接符号带下划线)。而 MicroPython 的
// shared/libc/printf.c 提供的是普通 snprintf/vsnprintf。本文件把 MinGW 期望
// 的符号转发到 MicroPython 实现, 让 readline.c 等引用的符号得以解析。
//
// 注意: 不能 #include <stdio.h>, 否则本文件自己的 snprintf 也会被重定向。
#include <stddef.h>
#include <stdarg.h>

// MicroPython shared/libc/printf.c 提供的实现
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

int __mingw_vsnprintf(char *s, size_t n, const char *fmt, va_list ap) {
    return vsnprintf(s, n, fmt, ap);
}

int __mingw_snprintf(char *s, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

// MinGW 的 <unistd.h> 内联 ftruncate 引用 MSVCRT 的 _chsize; 裸机无 MSVCRT。
// 提供 stub (文件截断功能内核用不到, 直接返回 -1 表示不支持)。
int _chsize(int fd, long size) {
    (void)fd; (void)size;
    return -1;
}
