bits 16
org 0x7C00
start:
    jmp 0x07C0:0x0072        ; literal
    jmp 0x07C0:(0x72)        ; paren literal
RM_OFF equ 0x72
    jmp 0x07C0:RM_OFF        ; %define const
    jmp 0x07C0:(.target)     ; label expr
.target:
    nop
