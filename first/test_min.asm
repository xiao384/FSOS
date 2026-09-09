; test_min.asm - 最小 COM1 输出测试 (仅写 'A' 到串口, 无磁盘/无保护模式)
bits 16
org 0x7C00
start:
    cli
    xor ax, ax
    mov ds, ax
.loop:
    mov dx, 0x3FD          ; LSR
.wait:
    in al, dx
    test al, 0x20          ; THRE
    jz .wait
    mov dx, 0x3F8          ; THR
    mov al, 'A'
    out dx, al
    jmp .loop
times 510 - ($ - $$) db 0
dw 0xAA55
