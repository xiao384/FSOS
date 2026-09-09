ENTRY(_start)

SECTIONS {
    . = 1M;                      /* 内核加载到 1MB 处 */

    /* multiboot 头必须位于镜像最前 (GRUB 要求前 8KB 内) */
    .text : ALIGN(4K) {
        KEEP(*(.multiboot))
        *(.text*)
    }

    /* 顺序: .data 放 .rodata 前, 让可写数据落在内核镜像第一段
       (bin 偏移 < 0x90000), 缩小需经引导扇区第二段装载的部分 */
    .data : ALIGN(16) {
        *(.data*)
    }

    .rodata : ALIGN(16) {
        *(.rodata*)
        *(.rdata*)           /* MinGW/PE 目标下只读数据节名 */
    }

    .bss : ALIGN(16) {
        __bss_start = .;
        *(.bss*)
        __bss_end = .;
    }

    . = ALIGN(4K);
    __kernel_end = .;            /* 内核结束地址，可用于内存管理器 */
    ___kernel_end = .;           /* MinGW(PE) 目标下 C 符号自动加前导下划线 */

    /* 丢弃不需要的节，保持内核镜像精简 (含 PE 自动生成的 .reloc) */
    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.reloc)
    }
}