bits 16
org 0x7C00
start:
    jmp 0x18:(.rm - start)
.rm:
    hlt
