// io.h - 裸机 x86 I/O 端口操作封装 (freestanding)
#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

// 批量 16 位端口读写
static inline void ins_words(uint16_t port, void* dst, uint32_t words) {
    __asm__ volatile("cld; rep insw" : "+D"(dst), "+c"(words) : "d"(port));
}
static inline void outs_words(uint16_t port, const void* src, uint32_t words) {
    __asm__ volatile("cld; rep outsw" : "+S"(src), "+c"(words) : "d"(port));
}

#endif // IO_H
