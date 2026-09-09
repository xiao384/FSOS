; boot.asm - 16-bit 引导扇区 (BIOS, 512B) - 两级引导的第一级
; 职责: 开启 A20, 读取 loader (LBA 1 起, LOADER_SECTORS 扇区) 到 0x7E00, 跳转。
; 第二级 loader (loader.asm) 负责读内核/页表/长模式切换, 空间充足。
;
; 构建注入: LOADER_SECTORS (默认 8)
bits 16
org 0x7C00

%ifndef LOADER_SECTORS
  %define LOADER_SECTORS 8
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; 保存 BIOS 传入的启动盘号: 供自身 int13h 使用 + 存入约定地址 0x500 (loader 读取)
    mov [boot_drv], dl
    mov [0x500], dl

    ; ---- 开启 A20 (快速通道: 端口 0x92) ----
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; ---- 探测 int13h 扩展读 (AH=0x41), 决定 LBA/CHS 路径 ----
    call probe_ext

    ; ---- 读取 loader 到 0x7E00 (LBA 1 起) ----
    ; 硬盘 (dl>=0x80) 优先用 LBA 扩展读 (AH=0x42); 失败则回退 CHS (VMware 兼容)
    ; 软盘/El Torito 仿真 (dl<0x80): SeaBIOS 仿真盘不支持 LBA 扩展读, 直接用 CHS
    cmp byte [boot_drv], 0x80
    jae .lba_loader
    call chs_read_loader
    jc boot_err
    jmp 0:0x7E00
.lba_loader:
    mov byte [dap_size], 0x10
    mov word [dap_count], LOADER_SECTORS
    mov word [dap_offset], 0x7E00
    mov word [dap_segment], 0
    mov dword [dap_lba], 1
    mov si, dap
    mov dl, [boot_drv]
    mov ah, 0x42
    int 0x13
    jnc .loaded
    ; LBA 失败 -> CHS 回退 (部分 VMware/BIOS 的扩展读不可用)
    call chs_read_loader
    jc boot_err
.loaded:
    ; 跳转第二级 loader
    jmp 0:0x7E00

boot_err:
    call print_err_boot
    hlt
    jmp boot_err

; ============================================================
; CHS 读取 loader: 镜像 LBA 1 起 LOADER_SECTORS 扇区 -> 0x7E00
; AH=0x08 查几何, LBA->CHS 后 AH=0x02 读。返回 CF=1 出错。
; ============================================================
chs_read_loader:
    mov ah, 0x08
    mov dl, [boot_drv]
    int 0x13
    jc .fail
    ; CL bits5-0 = spt, DH = max head
    movzx ax, cl
    and al, 0x3F
    mov [chs_spt], ax
    movzx ax, dh
    inc ax
    mov [chs_heads], ax
    ; LBA=1 -> CHS: sector=(1%spt)+1, head=(1/spt)%heads, cyl=1/(spt*heads)
    mov ax, 1
    xor dx, dx
    div word [chs_spt]
    push dx                  ; sector-1
    xor dx, dx
    div word [chs_heads]     ; ax=cyl, dx=head
    push dx                  ; head
    push ax                  ; cyl
    pop bx                   ; bx = cyl
    mov ch, bl               ; CH = cyl low 8
    mov ax, bx
    shr ax, 8
    and al, 0x03
    shl al, 6                ; al = (cyl>>8 & 3) << 6
    push ax
    pop dx                   ; dl = cyl 高 2 位
    pop bx                   ; bx = head
    mov dh, bl               ; DH = head
    pop bx                   ; bx = sector-1
    inc bx
    mov cl, bl               ; CL = sector (1-based)
    or cl, dl
    ; 读取 LOADER_SECTORS 扇区到 0x7E00
    xor ax, ax
    mov es, ax
    mov bx, 0x7E00
    mov ax, 0x0200 | LOADER_SECTORS
    mov dl, [boot_drv]
    int 0x13
    ret
.fail:
    stc
    ret

; ---- DAP (Disk Address Packet) ----
align 4
dap:
dap_size    db 0x10
            db 0
dap_count   dw LOADER_SECTORS
dap_offset  dw 0x7E00
dap_segment dw 0
dap_lba     dd 1
            dd 0
boot_drv    db 0
ext_ok      db 0

; ============================================================
; 探测 int13h 扩展磁盘访问 (AH=0x41):
;   成功且 cx 的 bit0(扩展读)置位 -> ext_ok=1, 否则 0
; VMware/部分 BIOS 需要先握手才会开放 AH=0x42
; ============================================================
probe_ext:
    mov byte [ext_ok], 0
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drv]
    int 0x13
    jc .pe_done
    cmp bx, 0xAA55
    jne .pe_done
    test cx, 0x01
    jz .pe_done
    mov byte [ext_ok], 1
.pe_done:
    ret

; 在 VGA 文本模式(0xB8000)打印可见错误, 避免黑屏无提示
print_err_boot:
    push es
    push di
    push si
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov si, err_msg_boot
.peb_loop:
    lodsb
    test al, al
    jz .peb_done
    mov ah, 0x0F        ; 白字黑底
    stosw
    jmp .peb_loop
.peb_done:
    pop si
    pop di
    pop es
    ret
err_msg_boot db "FSOS BOOT ERROR: disk read failed", 0

; CHS 几何 (软盘/仿真路径用, AH=0x08 查询)
chs_spt     dw 18
chs_heads   dw 2

; ---- 引导扇区结束标志 ----
times 510 - ($ - $$) db 0
dw 0xAA55
