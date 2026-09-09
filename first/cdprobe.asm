bits 16
org 0x7C00
start:
  mov ah, 0x41
  mov bx, 0x55AA
  int 0x13
  jc .noext
  cmp bx, 0xAA55
  jne .noext

  ; 读取 LBA 16 到 0x1000, 检查 [0x1001] 是否为 'C' (PVD "CD001")
  mov dword [pkt+8], 16
  mov si, pkt
  mov ah, 0x42
  int 0x13
  jc .fail
  mov al, [0x1001]
  cmp al, 0x43        ; 'C'
  je .pvd2048
  mov al, 'x'          ; LBA16 不是 2048 单位
  out 0xE9, al
  jmp $

.pvd2048:
  mov al, 'P'          ; LBA 与 ISO 扇区同为单位(2048)
  out 0xE9, al
  ; 再读 LBA 24 校验引导镜像首字节 0xFA
  mov dword [pkt+8], 24
  mov si, pkt
  mov ah, 0x42
  int 0x13
  jc .fail
  mov al, [0x1000]
  cmp al, 0xFA
  je .ok
  mov al, 'b'          ; LBA24 不是引导镜像
  out 0xE9, al
  jmp $

.ok:
  mov al, 'O'          ; 全部对上: LBA=2048单位, LBA24=引导镜像
  out 0xE9, al
  jmp $

.noext:
  mov al, 'N'
  out 0xE9, al
  jmp $
.fail:
  mov al, 'F'
  out 0xE9, al
  jmp $

pkt:
  db 0x10
  db 0
  dw 1
  dw 0x1000, 0x0000
  dq 0
  times 510-($-$$) db 0
  dw 0xAA55
