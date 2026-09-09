bits 16
org 0x7C00
rm_unreal:
    nop
RM_OFF equ (rm_unreal - start)
start:
    jmp 0x07C0:RM_OFF
