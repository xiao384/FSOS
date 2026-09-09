/* mod_cint.ld - cint 模块固定地址链接脚本
 * 入口 module_entry 紧跟 16 字节 mod_header_t(.modhdr 段, 偏移 0) 之后,
 * 内核据此 (va + header.entry_off) 跳入。所有引用在链接期解析为相对 0x40000000 的
 * 绝对地址, 故 blob 读入该 VA 后无需重定位即可执行。 */
SECTIONS {
    . = 0x40000000;
    .hdr   : { KEEP(*(.modhdr)) }
    .text ALIGN(1) : { KEEP(*(.module_entry)) *(.text*) }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data  : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.reloc) }
}
ENTRY(module_entry)
