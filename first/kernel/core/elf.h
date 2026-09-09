// elf.h - ELF64 加载器 (阶段 3)
//
// 解析静态链接的 ELF64 可执行文件, 把 PT_LOAD 段加载到用户虚拟地址,
// 返回入口地址。调用者负责分配用户栈并 iretq 到 ring3 执行。
#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>

// 加载 ELF64 到用户空间:
//   elf_data = ELF 文件内容, size = 字节数
//   返回入口地址 (e_entry), 0 = 失败
// 副作用: 把 PT_LOAD 段 memcpy 到 p_vaddr, BSS 区清零
uint64_t elf_load(const void* elf_data, size_t size);

// 验证 ELF 头合法性 (magic + ELF64 + x86-64 + ET_EXEC), 0=合法, -1=非法
int elf_validate(const void* elf_data, size_t size);

#endif // ELF_H