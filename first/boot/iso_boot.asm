; iso_boot.asm - El Torito No-Emulation 引导加载器 (小体积)
;
; 关键约束: SeaBIOS 的 no-emulation 引导只能把引导镜像载入常规内存(<1MB),
; 因此绝不能把 2.8MB 的内核(含内嵌 image.img)整体 %incbin 进本镜像——大镜像
; 会被 BIOS 直接拒绝(启动无任何输出)。
;
; 本加载器自身 < 64KB, 由 no-emulation 正常载入 0x7C00 (段 0x07C0)。随后:
;   1. 开 A20, 用 int10h 拷贝 8x8 字体到 0xB0000 (实模式, 依赖 BIOS);
;   2. 进入 "unreal 模式" (PM 下把数据段限长设为 4GB, 再退回实模式),
;      使 int13h 能把 CD 上的内核直接读到高位内存 0x100000+;
;   3. 用 int13h 扩展读(AH=0x42, LBA 以 2048 字节扇区计)把 kernel.bin 读入 0x100000;
;   4. 建 4 级页表, 进 64 位长模式, 跳 KERNEL_ENTRY。
;
; 构建时由 pack_iso.ps1 注入:
;   KERNEL_ENTRY : 内核入口 (64 位)
;   KERNEL_LBA   : kernel.bin 在 ISO 中的起始 LBA (2048 字节扇区)
;   KERNEL_SECT  : kernel.bin 占用的 ISO 扇区数 (ceil(size/2048))
bits 16
org 0x7C00

%ifndef KERNEL_ENTRY
  %define KERNEL_ENTRY 0x101020
%endif
%ifndef KERNEL_LBA
  %define KERNEL_LBA 0
%endif
%ifndef KERNEL_SECT
  %define KERNEL_SECT 1
%endif

; 低位暂存区 (int13h 的 DAP 只能用 段:偏移 寻址, 最大约 1.1MB)
STAGE_SEG  equ 0x1000            ; 0x1000:0x0000 = 物理 0x10000
STAGE_OFF  equ 0x0000
CHUNK_SECT equ 32                ; 每次 32 扇区 = 64KB

; 返回实模式时用的远跳偏移 (rm_unreal 在文件中的偏移, 非绝对地址)
; 注意: 不能用 `%define RM_OFF (rm_unreal - start)` —— NASM 会把该字面量
; 重新展开成 `jmp 0x07C0:(rm_unreal - start)` 的"括号标签表达式"形式, 从而
; 把段值 0x07C0 误解析为段覆盖前缀并丢弃(编码成 jmp 0:0x72)。
; 正确做法: 用 `equ` 常量, 且必须定义在 rm_unreal 标签 *之后*(使 rm_unreal
; 在求值 equ 时已可见), 见文件末尾附近的 RM_OFF equ 定义。

; ---- 调试: 同时输出到 QEMU 0xE9 调试端口与 COM1 (VMware 串口 0x3F8) ----
; 注意: NASM 中 `;` 是注释符, 因此不能用 `%define MARK(c) a; b` 的形式,
; 必须用 %macro 才能包含多条指令。
%macro MARK 1
    push dx
    push cx
    push ax
    mov al, %1
    out 0xE9, al                ; QEMU -debugcon 可捕获
    mov cx, 0x1000              ; 超时计数, 避免 COM1 未初始化时死等
    mov dx, 0x3FD               ; COM1 LSR
%%wait:
    in al, dx
    test al, 0x20               ; THRE 空?
    jnz %%send
    dec cx
    jnz %%wait
    jmp %%done                  ; 超时: 跳过 COM1 输出
%%send:
    mov dx, 0x3F8               ; COM1 THR
    mov al, %1
    out dx, al
%%done:
    pop ax
    pop cx
    pop dx
%endmacro

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drv], dl          ; 保存 BIOS 传入的启动盘号(CD-ROM)
    MARK 0x31                   ; '1' iso_boot 启动

%ifndef BISECT_NO_EARLY
    ; 开启 A20 (快速端口)
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; ---- 拷贝 VGA ROM 8x8 字体到 0xB0000 (必须在实模式下用 int10h) ----
    push es
    mov ax, 0x1130
    mov bh, 0x03
    int 0x10
    mov ax, es
    mov ds, ax
    mov si, bp
    mov ax, 0xB000
    mov es, ax
    xor di, di
    mov cx, 512
    rep movsw
    pop es
    xor ax, ax
    mov ds, ax
    mov es, ax
%endif
    MARK 0x32                   ; '2' 早期初始化完成

    ; ---- 进入 unreal 模式: PM 下把数据段限长设为 4GB, 再退回实模式 ----
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp 0x08:.pm_unreal
bits 32
.pm_unreal:
    mov ax, 0x10                ; 4GB 数据段 (base 0)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7C00
    MARK 0x37                   ; '7' 已进入 32 位 PM (unreal 准备)
    ; 退回实模式, 但保留段描述符缓存里的 4GB 限长 -> unreal 模式。
    ; 返回用的远跳: 段选择子必须是合法 GDT 项。0x18 是 base=0x7C00 的 32 位
    ; 代码段; 其 IP 取文件偏移(.rm_unreal - start), 于是物理地址 =
    ; 0x7C00 + 文件偏移 = 加载器内 .rm_unreal 的真实位置。
    MARK 0x38                   ; '8' 即将清除 PE 退回实模式
    mov eax, cr0
    and eax, ~0x01
    mov cr0, eax
    ; !! 关键: 清 PE 后必须有一条"序列化指令"提交该写, 否则紧随其后的远跳
    ;    仍会在 PM 下执行(把 0x07C0 当 GDT 选择子 -> #GP -> 死机)。读取 CR0
    ;    本身是序列化指令, 足以让 PE=0 在远跳前生效。
    mov eax, cr0                ; 序列化: 提交 PE 清零
    ; 再插入一次序列化 + 近跳, 强制 QEMU 结束当前翻译块(TB), 使下面的远跳在
    ; "PE 已清零"的新 TB 中被翻译(否则仍在 PM 下解析段值 -> #GP -> 死机)。
    jmp .flush
.flush:
    ; 返回实模式: 直接远跳到实模式段 0x07C0, IP 取文件偏移 RM_OFF。
    ; 注意: 在实模式下远跳的段值被当作"原始段值"解释 (不是 GDT 选择子),
    ; 于是物理地址 = 0x07C0*16 + RM_OFF = 0x7C00 + RM_OFF = rm_unreal 的真实位置。
    ; 关键: 该远跳必须以 32 位上下文(bits 32)编码为 ptr16:32
    ; (EA + 32 位偏移 + 16 位段, 共 6 字节)。实测若切到 bits 16, NASM 会编码成
    ; 5 字节的 ptr16:16, 执行时无法正确返回实模式(死机)。因此此处不能加 bits 16。
    MARK 0x2E                   ; '.' 端口写: 强制 QEMU 退出翻译块, 提交 PE=0
    jmp 0x07C0:(rm_unreal - start)
bits 16
rm_unreal:
    MARK 0x39                   ; '9' 已回到实模式并落到 rm_unreal (PE 清除+远跳成功)
    ; 诊断: 回读 CR0, 确认 PE 是否真的清除了
    mov eax, cr0
    test al, 1
    jz .pe_ok
    MARK 0x50                   ; 'P' PE 仍置位 (远跳后仍在 PM!)
    jmp .pe_done
.pe_ok:
    MARK 0x51                   ; 'Q' PE 已清除
.pe_done:
    ; 回到实模式后, ds/es/ss 仍可能是 PM 选择子(0x10); 在实模式下会被当作
    ; 实模式段值(0x100)解释, 导致 org 0x7C00 的数据地址错位。复位为 0。

; RM_OFF 必须定义在 rm_unreal 标签之后: 这样 `equ` 求值时 rm_unreal 已可见,
; 得到正确文件偏移; 否则 (equ 在标签前) 会得到 0xFFFF 错误偏移。
RM_OFF equ (rm_unreal - start)
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    MARK 0x33                   ; '3' 进入 unreal 模式 (可访问高位内存)
    MARK 0x61                   ; 'a' 进入 CD 读阶段

    ; ---- 用 int13h 扩展读把 kernel.bin 读入 0x100000 (高位内存) ----
    xor ebx, ebx               ; 已读扇区数(相对 KERNEL_LBA)
    mov ecx, KERNEL_SECT
    mov dword [copy_off], 0    ; 已搬入高位内存的字节数
.load_loop:
    MARK 0x62                   ; 'b' 一次读迭代开始
    ; 本次扇区数 = min(CHUNK_SECT, ecx)
    MARK 0x21                   ; '!' 已过 'b'
    mov eax, CHUNK_SECT
    cmp ecx, eax
    jbe .last
    MARK 0x22                   ; '"' 走完整块分支
    mov word [pkt_count], CHUNK_SECT
    jmp .do
.last:
    MARK 0x23                   ; '#' 走尾块分支
    mov word [pkt_count], cx
.do:
    MARK 0x24                   ; '$' 到达 .do
    ; 包: LBA = KERNEL_LBA + ebx (缓冲字段固定为暂存区, 无需每轮重算)
    mov eax, KERNEL_LBA
    add eax, ebx
    mov [pkt_lba], eax
    MARK 0x25                   ; '%' LBA 已写入包
    ; int13h 扩展读到暂存区 STAGE_SEG:STAGE_OFF
    mov si, pkt
    mov dl, [boot_drv]
    mov ah, 0x42
    MARK 0x4C                   ; 'L' 即将调用 int13h
    int 0x13
    MARK 0x4D                   ; 'M' int13h 已返回
    jc .read_fail
    MARK 0x63                   ; 'c' 一次读成功
    ; ---- 把暂存区里的本块搬到 0x100000 + copy_off ----
    ; int13h 的 DAP 缓冲是 段:偏移 远指针, 只能寻址常规内存, 所以先读到
    ; 0x10000, 再切进保护模式用 4GB 扁平段拷到 1MB 以上, 退回实模式读下一块。
    ; (不用 unreal 模式的 fs/gs: BIOS 允许在 int13h 中破坏 fs/gs,
    ;  跨 BIOS 调用依赖其隐藏描述符在 SeaBIOS/VMware 上表现不一致)
    movzx eax, word [pkt_count]
    mov [chunk_n], eax
    call pm_copy_chunk
    movzx eax, word [pkt_count]
    shl eax, 11                 ; * 2048 = 本次搬运字节数
    add [copy_off], eax
    MARK 0x65                   ; 'e' 本块已搬入高位内存
    ; 推进
    movzx eax, word [pkt_count]
    add ebx, eax
    sub ecx, eax
    jnz .load_loop
    MARK 0x34                   ; '4' 内核已从 CD 读入 0x100000
    MARK 0x64                   ; 'd' 即将进 PM
    jmp to_pm                   ; 全部读完并搬完, 进保护模式

.read_fail:
    MARK 0x46                   ; 'F' CD 读失败
    hlt

; ============================================================
; 保护模式小程序: 把暂存区 0x10000 处 chunk_n 个 2048 字节扇区
; 拷到 0x100000 + copy_off, 然后退回实模式返回调用者。
; 调用约定: 实模式下 call pm_copy_chunk, 内部自管 RM<->PM 切换。
; 返回实模式复用文件上方已验证过的 ptr16:32 远跳 (段 0x07C0 + 文件偏移)。
; ============================================================
bits 16
pm_copy_chunk:
    ; 必须保存/恢复通用寄存器: 内部 rep movsd 会改写 ecx/esi/edi,
    ; 而调用方用 ecx 记录剩余扇区数、ebx 记录已读扇区数。
    pushf
    pushad
    cli
    mov [save_sp], sp            ; 指向 pushad 帧顶, 返回时据此复原
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp 0x08:.pmc
bits 32
.pmc:
    mov ax, 0x10                ; 4GB 扁平数据段 (base 0)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000
    mov ecx, [chunk_n]
    shl ecx, 9                  ; chunk_n * 2048 / 4 = chunk_n * 512 (双字数)
    mov esi, 0x10000            ; 源: 暂存区 (STAGE_SEG:STAGE_OFF)
    mov edi, [copy_off]
    add edi, 0x100000           ; 目的: 内核加载基址 + 已搬运字节数
    cld
    rep movsd
    ; 退回实模式 (与 unreal 返回同一套路: 先序列化提交 PE=0, 再 ptr16:32 远跳)
    mov eax, cr0
    and eax, ~0x01
    mov cr0, eax
    mov eax, cr0                ; 序列化
    jmp .pmc_flush
.pmc_flush:
    jmp 0x07C0:(pmc_rm - start) ; 物理地址 = 0x7C00 + 文件偏移
bits 16
pmc_rm:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, [save_sp]           ; 回到 pushad 帧顶
    popad
    popf                        ; 还原 IF 等标志
    ret

to_pm:
    ; ---- 进入 32 位保护模式, 建页表 -> 进长模式 ----
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp 0x08:pm_start

; ============================================================
bits 32
pm_start:
    mov ax, 0x10                ; 4GB 数据段 (base 0, 扁平)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000

    ; ---- 清零页表区 0x9000-0xEFFF (含 4 个 PD: PD0/PD1/PD2/PD3) ----
    ; 必须覆盖全部 4 个 PD (0xB000-0xF000), 否则 PD1~PD3 表项高 32 位残留
    ; 垃圾 -> 映射错乱 -> 开启分页后 #PF。
    cld
    xor eax, eax
    mov edi, 0x9000
    mov ecx, 0x6000 / 4       ; 24KB = 6144 dword, 覆盖 0x9000-0xEFFF
    rep stosd

    ; ---- 构建 4 级页表, 2MB 大页映射前 4GB ----
    ; 必须覆盖到 4GB 以包含 VMware SVGA 线性帧缓冲(高物理地址), 否则
    ; 内核访问 LFB 会 #PF -> 三连崩溃 (VMware: "tried to execute an invalid
    ; part of memory")。QEMU 的 std VGA 帧缓冲在 0xA0000(已覆盖), 不受影响。
    mov dword [0x9000], 0xA003  ; PML4[0] -> PDPT
    mov dword [0xA000], 0xB003  ; PDPT[0] -> PD0
    mov dword [0xA008], 0xC003  ; PDPT[1] -> PD1
    mov dword [0xA010], 0xD003  ; PDPT[2] -> PD2
    mov dword [0xA018], 0xE003  ; PDPT[3] -> PD3
    mov eax, 0x83               ; P + RW + PS(2MB 页)
    mov edi, 0xB000
    mov ecx, 512*4             ; 4 个 PD * 512 项 = 覆盖 0..4GB
.build_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .build_pd
    MARK 0x35                   ; '5' 页表构建完成

    ; ---- 启用长模式 ----
    mov eax, cr4
    or eax, 0x20                ; PAE
    mov cr4, eax
    mov ecx, 0xC0000080         ; EFER
    rdmsr
    or eax, 0x100               ; LME
    xor edx, edx
    wrmsr
    mov eax, 0x9000
    mov cr3, eax
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 0x80000001          ; PG | PE
    mov cr0, eax
    MARK 0x36                   ; '6' 已开分页, 跳内核
    jmp far [pm64_far]

bits 64
pm64_start:
    mov ax, 0x28
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x1D000000
    mov rax, KERNEL_ENTRY
    jmp rax

; 跳转到 64 位代码的远指针 (16 位段选择子 + 32 位偏移=pm64_start 的绝对地址)
pm64_far:
    dw 0x20
    dd pm64_start

; ============================================================
; 单一 GDT (实模式加载一次即可, 全程复用)
;   0x08    32 位代码, base 0
;   0x10    32 位数据, base 0, 4GB 限长 (扁平)
;   0x18    32 位代码, base 0x7C00, 4GB 限长 (unreal 返回用)
;   0x20    64 位代码, base 0
;   0x28    64 位数据, base 0
;   0x07C0  16 位代码, base 0, limit 0xFFFF
;           注: 清 PE 后远跳回实模式时, 段值 0x07C0 在 PM 下会被当 selector 检查,
;           因此必须在 GDT 偏移 0x07C0 处放一条合法描述符, 否则在 VMware/QEMU
;           严格实现上会 #GP/triple fault。
align 4
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF        ; 0x08: 32-bit code, base 0, limit 4G
    dq 0x00CF92000000FFFF        ; 0x10: 32-bit data, base 0, limit 4G
    dq 0x00CF9A00007C00FF        ; 0x18: 32-bit code, base 0x7C00, limit 4G
    dq 0x0020980000000000        ; 0x20: 64-bit code, base 0
    dq 0x0000920000000000        ; 0x28: 64-bit data, base 0
    ; padding to selector 0x07C0 (index 0xF8 -> GDT offset 0x7C0)
    times (0x7C0 - ($ - gdt)) db 0
    dq 0x00009A007C00FFFF        ; 0x07C0: 16-bit code, base 0x7C00, limit 0xFFFF
                                 ; 关键: 清 PE 后的远跳会被 CPU 在 PM 下用 GDT 选择子解析,
                                 ; 该描述符 base 必须为 0x7C00(等于段 0x07C0 * 16), 才能让
                                 ; 目标线性地址与实模式地址一致(0x7C00+偏移)。base=0 会导致
                                 ; 跳到 IVT/BIOS 数据区 -> triple fault。
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt

; ---- int13h 扩展读数据包 (16 字节) ----
pkt:
    db 0x10                      ; 包大小
    db 0
pkt_count:
    dw 32                        ; 本次读扇区数 (32*2048=64KB)
pkt_buf:
    ; !! DAP 的"传输缓冲"字段是 段:偏移 远指针(低字=偏移, 高字=段), 不是 32 位线性地址。
    ;    原先填 dd 0x00100000 会被解释为 偏移0x0000 段0x0010 -> 物理 0x100, 直接覆盖 IVT 死机。
    ;    这里用低位暂存区 0x1000:0x0000 = 物理 0x10000, 读后再搬去高位内存。
    dw 0x0000                    ; 偏移
    dw STAGE_SEG                 ; 段 (0x1000 -> 0x10000)
pkt_lba:
    dq 0                         ; LBA(2048 字节扇区); 高位初始化为 0

boot_drv db 0

; ---- 块加载搬运状态 (见 pm_copy_chunk) ----
align 4
copy_off dd 0                    ; 已搬入高位内存的字节数 (目的 = 0x100000 + copy_off)
chunk_n  dd 0                    ; 本次搬运的 2048 字节扇区数
save_sp  dw 0                    ; 进 PM 前保存的实模式 SP
