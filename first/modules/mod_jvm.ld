/* mod_jvm.ld - jvm 模块固定地址链接脚本 (同 mod_cint.ld, 仅基址不同: 0x80000000) */
SECTIONS {
    . = 0x80000000;
    .hdr   : { KEEP(*(.modhdr)) }
    .text ALIGN(1) : { KEEP(*(.module_entry)) *(.text*) }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data  : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.reloc) }
}
ENTRY(module_entry)
