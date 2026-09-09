bits 16
org 0x7C00
start:
  ; DL = 启动盘号 (CD-ROM), 由 BIOS 在跳转时传入
  ; 1) 检查 int13h 扩展
  mov ah, 0x41
  mov bx, 0x55AA
  int 0x13
  jc .noext
  cmp bx, 0xAA55
  jne .noext

  ; 2) 扩展读: 把 LBA 24 (本引导镜像所在扇区) 读回 0x1000
  mov si, pkt
  mov ah, 0x42
  int 0x13
  jc .fail

  ; 3) 校验: 0x1000 处首字节应 == 本镜像首字节(0xFA=cli)
  mov al, [0x1000]
  cmp al, 0xFA
  je .ok
  mov al, 'W'          ; 读到但内容不符
  out 0xE9, al
  jmp $

.ok:
  mov al, 'K'          ; int13h 扩展读成功
  out 0xE9, al
  jmp $

.noext:
  mov al, 'N'          ; 无扩展
  out 0xE9, al
  jmp $

.fail:
  mov al, 'F'          ; 读失败 (AH 在 int13 后)
  out 0xE9, al
  jmp $

pkt:
  db 0x10             ; packet size
  db 0
  dw 1                ; blocks
  dw 0x1000, 0x0000   ; buffer = seg:off = 0x0000:0x1000
  dq 24               ; LBA (2048 字节扇区)
  times 510-($-$$) db 0
  dw 0xAA55
