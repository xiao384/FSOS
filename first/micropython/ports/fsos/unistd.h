// unistd.h - 裸机替代 MinGW 版本
//
// MicroPython 的多个源文件 include <unistd.h> 只是为了 ssize_t 和 SEEK_*。
// MinGW 的 unistd.h 里内联 ftruncate 引用 MSVCRT 的 _chsize,
// 在 freestanding/裸机编译下会报 implicit-declaration 错误。
// ssize_t 已由 MinGW 的 <corecrt.h> (经 <stdio.h> 间接包含) 提供,
// 因此这里只补充 SEEK_* 常量并屏蔽系统 unistd.h。
#ifndef FSOS_UNISTD_H
#define FSOS_UNISTD_H

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#endif /* FSOS_UNISTD_H */
