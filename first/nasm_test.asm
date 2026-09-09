bits 32
section .text
global _foo0
extern _bar
_foo0:
    nop
    jmp _bar
