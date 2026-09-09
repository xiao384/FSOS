bits 16
org 0x7C00
start:
  mov al, 0x41
  out 0xE9, al
  jmp start
  times 510-($-$$) db 0
  dw 0xAA55
