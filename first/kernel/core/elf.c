// elf.c - ELF64 加载器实现 (阶段 3)
#include "elf.h"
#include <stdint.h>
#include <stddef.h>

// ELF64 数据结构
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

#define ET_EXEC   2
#define EM_X86_64 0x3E
#define PT_LOAD   1

int elf_validate(const void* elf_data, size_t size) {
    if (!elf_data || size < sizeof(elf64_ehdr_t)) return -1;
    const uint8_t* p = (const uint8_t*)elf_data;
    // magic: 0x7F 'E' 'L' 'F'
    if (p[0] != 0x7F || p[1] != 'E' || p[2] != 'L' || p[3] != 'F') return -1;
    // ELFCLASS64 = 2
    if (p[4] != 2) return -1;
    // ELFDATA2LSB = 1
    if (p[5] != 1) return -1;

    const elf64_ehdr_t* eh = (const elf64_ehdr_t*)elf_data;
    if (eh->e_type != ET_EXEC) return -1;
    if (eh->e_machine != EM_X86_64) return -1;
    if (eh->e_phnum == 0 || eh->e_phoff == 0) return -1;
    return 0;
}

uint64_t elf_load(const void* elf_data, size_t size) {
    if (elf_validate(elf_data, size) != 0) return 0;

    const elf64_ehdr_t* eh = (const elf64_ehdr_t*)elf_data;
    const elf64_phdr_t* ph = (const elf64_phdr_t*)((const uint8_t*)elf_data + eh->e_phoff);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t* seg = &ph[i];
        if (seg->p_type != PT_LOAD) continue;

        // 边界检查
        if (seg->p_offset + seg->p_filesz > size) return 0;
        if (seg->p_vaddr == 0) continue;

        // 复制 p_filesz 字节到 p_vaddr
        uint8_t* dst = (uint8_t*)(uintptr_t)seg->p_vaddr;
        const uint8_t* src = (const uint8_t*)elf_data + seg->p_offset;
        for (uint64_t j = 0; j < seg->p_filesz; j++) dst[j] = src[j];

        // BSS 区清零 (p_memsz > p_filesz 的部分)
        for (uint64_t j = seg->p_filesz; j < seg->p_memsz; j++) dst[j] = 0;
    }

    return eh->e_entry;
}