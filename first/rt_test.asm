; rt_test.asm - 诊断: 清 PE 后回读 CR0, 输出 P=仍置位 / p=已清除
bits 16
org 0x7C00
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov al, '1'
    out 0xE9, al
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:.pm
bits 32
.pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7C00
    mov al, '2'
    out 0xE9, al
    mov eax, cr0
    and eax, ~1
    mov cr0, eax
    ; 回读 CR0, 检查 PE 是否仍置位
    mov eax, cr0
    test al, 1
    jz .pe0
    mov al, 'P'                  ; PE 仍置位 -> 远跳会在 PM 下执行(段值被当选择子)
    out 0xE9, al
    jmp .hang
.pe0:
    mov al, 'p'                  ; PE 已清除
    out 0xE9, al
    jmp 0x07C0:(.rm - start)
.rm:
    bits 16
    mov al, '3'
    out 0xE9, al
    hlt
.hang:
    hlt
gdt:
    dq 0
    dq 0x00CF9A000000FFFF        ; 0x08: 32-bit code, base 0
    dq 0x00CF92000000FFFF        ; 0x10: 32-bit data, base 0
gdt_ptr:
    dw gdt_ptr - gdt - 1
    dd gdt
times 510-($-$$) db 0
dw 0xAA55
