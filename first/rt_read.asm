; rt_read.asm - 验证: 纯实模式下用 int13h 扩展读把扇区读入 0x100000 (32位线性缓冲)
; 若 BIOS 直接写入高位内存, 则无需 unreal 模式. 之后进 PM 读回该字节验证.
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
    ; 读 LBA 0, 1 扇区, 缓冲 0x0000:0x7E00 (低位内存, 物理 0x7E00)
    mov si, pkt
    mov dl, 0x00                ; 软盘 A:
    mov ah, 0x42
    int 0x13
    jc .fail
    mov al, '2'
    out 0xE9, al
    ; 在实模式下直接读回 0x7E00 处的字节 (DS=0, 偏移 0x7E00)
    mov ax, 0
    mov ds, ax
    mov al, [0x7E00]
    out 0xE9, al
.halt:
    hlt
.fail:
    mov al, 'F'
    out 0xE9, al
.h:
    hlt
pkt:
    db 0x10                      ; 包大小
    db 0
    dw 1                         ; 扇区数
    dd 0x00007E00               ; 32 位线性缓冲地址 (低位)
    dq 0                         ; LBA 0
gdt:
    dq 0
    dq 0x00CF9A000000FFFF        ; 0x08: 32-bit code, base 0
    dq 0x00CF92000000FFFF        ; 0x10: 32-bit data, base 0
gdt_ptr:
    dw gdt_ptr - gdt - 1
    dd gdt
times 510-($-$$) db 0
dw 0xAA55
