; loader.asm - 16-bit 第二级引导 (加载到 0x7E00)
; 职责: 分块读内核(传统 CHS int13h 经 unreal-mode ES 直接落盘到高位 0x100000+) ->
;       拷贝 8x8 字体 -> 进保护模式/长模式 -> 跳内核入口。
;
; 设计要点: 内核 > 1MB。每块先把 ES(0x28 选择子) 的段基址设为
; 0x100000 + copy_pos*512, 通过 "unreal mode" 使 ES 在实模式下仍带 4GB 限与
; 高位基址; 随后逐扇区调用传统 int13h(ah=0x02, 缓冲 ES:BX) 把数据直接写入高位
; 内存。全程不碰低内存, 避开 SeaBIOS 低内存占用的竞态。
;
; 关键修复: 旧方案先读入低内存缓冲(0x1000/0x10000)再切保护模式拷贝; SeaBIOS
; 运行期会清零/写入低内存 0x1000~0x90000, 与读缓冲竞态 -> 偶发内核被清 0 ->
; 启动失败。直接高位落盘后该竞态消失。EDD(ah=0x42) 的 32 位线性缓冲在本
; SeaBIOS 上对 >1MB 地址内部 #UD, 故改用传统 ah=0x02 + unreal ES。
;
; 低端内存布局:
;   loader 自身: 0x7E00-0x8E00
;   页表       : 0x9000-0xC000      (PML4@0x9000/PDPT@0xA000/PD@0xB000)
;   8x8 字体   : 0xB0000            (VGA 显存高 64KB, mode13h 不使用)
;
; 构建注入: KERNEL_LBA / KERNEL_ENTRY / KERNEL_SECTORS
bits 16
org 0x7E00

%ifndef KERNEL_LBA
  %define KERNEL_LBA 9
%endif
%ifndef KERNEL_ENTRY
  %define KERNEL_ENTRY 0x101000
%endif
%ifndef KERNEL_SECTORS
  %define KERNEL_SECTORS 4096
%endif
%ifndef CHUNK_SECTORS
  ; 每块 44 扇区 (22KB), 由 EDD int13h 单次直接落盘到高位内存。
  ; 单批上限取 127(0x7F, EDD 限制); 44 远小于此, read_batch 不会拆分。
  %define CHUNK_SECTORS 44
%endif

; 调试检查点: -d DEBUG 时向 COM1 输出进度字符
%ifdef DEBUG
%macro DBG 1
    push ax
    mov al, %1
    call com_out
    pop ax
%endmacro
%else
%macro DBG 1
%endmacro
%endif

; 内联版检查点: 不调用子程序, 16/32 位模式下指令编码通用 (只用 dx/al 端口 IO)。
%ifdef DEBUG
%macro DBGI 1
    push eax
    push edx
    mov ah, %1
%%dbgi_wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz %%dbgi_wait
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    pop edx
    pop eax
%endmacro
%else
%macro DBGI 1
%endmacro
%endif

; 64 位长模式下的内联检查点 (push eax 在 64 位不可编码, 需用 rax/rdx)
%ifdef DEBUG
%macro DBGQ 1
    push rax
    push rdx
    mov ah, %1
%%dbgq_wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz %%dbgq_wait
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    pop rdx
    pop rax
%endmacro
%else
%macro DBGQ 1
%endmacro
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    ; 读取 boot 存入约定地址 0x500 的启动盘号
    mov ax, [0x500]
    mov [boot_drv], al
    call probe_ext          ; 探测 int13h 扩展读, 结果存 lba_ok
    DBG '1'

    ; ---- 分块读内核: EDD(ah=0x42) 读入低缓冲 0x10000 -> 切保护模式拷到 1MB+ ----
    ; 内核 > 1MB, 无法在实模式 <1MB 整段暂存, 故每块先 int13h 读入 0x10000,
    ; 再经 pm_copy_chunk 整块拷到 0x100000 + 偏移, 循环直至内核连续落在 1MB 处。
    ; EDD 对低地址缓冲(0x10000)支持良好; 高位由 pm_copy_chunk 在保护模式下完成。
    mov dword [copy_pos], 0
.load_loop:
    DBG 'L'
    mov eax, [copy_pos]
    cmp eax, KERNEL_SECTORS
    jae .loaded
    ; 本块大小 = min(CHUNK, 剩余)
    mov ebx, KERNEL_SECTORS
    sub ebx, eax
    cmp ebx, CHUNK_SECTORS
    jbe .sz_ok
    mov ebx, CHUNK_SECTORS
.sz_ok:
    mov [chunk_n], ebx
    ; 读 ebx 扇区 (LBA = KERNEL_LBA + copy_pos) 到 0x10000
    mov dword [rd_base], 0x10000
    mov eax, [copy_pos]
    add eax, KERNEL_LBA
    mov dword [dap_lba], eax
    mov [remaining], bx
    DBG 'r'
    call read_batch
    DBG 'R'
    ; 经保护模式小程序把本块拷到 0x100000 + copy_pos*512
    DBG 'p'
    call pm_copy_chunk
    DBGI 'P'
    ; 推进
    mov eax, [copy_pos]
    add eax, [chunk_n]
    mov [copy_pos], eax
    DBG '2'
    jmp .load_loop

.loaded:
    DBG '4'

    ; ---- 探测并设置 VBE 640x480x32 LFB 模式 ----
    ; 成功则把帧缓冲信息写入 0x6400 (magic 'VBE2'), gfx_init() 激活 LFB 路径;
    ; 失败则跳过, gfx_init() 回退 mode13h 320x200x256。
    call vbe_setup

    ; ---- 拷贝 VGA ROM 8x8 字体到 0xB0000 (图形界面用) ----
    ; 0xB0000 是 VGA 显存高 64KB, mode13h (0xA0000-0xAFFFF) 不使用。
    push es
    mov ax, 0x1130
    mov bh, 0x03              ; 8x8 ROM 字体
    int 0x10                  ; 返回 ES:BP 指向字体
    mov ax, es
    mov ds, ax
    mov si, bp
    mov ax, 0xB000
    mov es, ax
    xor di, di
    mov cx, 512               ; 1024 字节 / 2
    rep movsw
    pop es
    xor ax, ax
    mov ds, ax

    ; ---- 进入保护模式, 构建页表, 进长模式 ----
    lgdt [gdt_ptr]
    DBG '5'
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp 0x08:pm_start            ; 远跳刷新预取, 进入 32 位

; ============================================================
; 32 位保护模式: 构建页表 -> 进长模式
; ============================================================
bits 32
pm_start:
    mov ax, 0x10                ; 数据段选择子
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000            ; 临时栈(低于 1MB)
    DBGI '6'

    ; ---- 清零页表区 0x9000-0xEFFF (含 4 个 PD: PD0/PD1/PD2/PD3) ----
    ; 关键! 页表构建只写每个 8 字节表项的低 4 字节 (mov [edi],eax), 高 4 字节
    ; 必须为 0。硬盘引导时该区域恰好是 0, 但 El Torito 引导时 SeaBIOS 会把
    ; 内核前 16KB 预载到 0x7C00-0xBBFF, 0x9000 起残留内核代码字节, 若不清零
    ; 2MB 大页表项的高 32 位变成垃圾 -> 映射错乱 -> 开启分页后立即 #PF。
    ; 覆盖: PML4@0x9000 / PDPT@0xA000 / PD0~PD3@0xB000/0xC000/0xD000/0xE000。
    cld
    xor eax, eax
    mov edi, 0x9000
    mov ecx, 0x6000 / 4       ; 24KB = 6144 dword, 覆盖 0x9000-0xEFFF
    rep stosd

    ; ---- 构建 4 级页表, 2MB 大页映射 0~4GB ----
    ; PML4 @ 0x9000, PDPT @ 0xA000, PD0~PD3 @ 0xB000/0xC000/0xD000/0xE000。
    ; VMware SVGA 线性帧缓冲在高物理地址 (常 >=0x40000000, 可达 ~0xE0000000),
    ; 仅映射 0~1GB 会导致访问 LFB 时 #PF (triple fault "tried to execute an
    ; invalid part of memory")。必须覆盖到 4GB 才能包含该 LFB。
    ;
    ; 内存隔离 (阶段 1):
    ;   0 - 512MB  : supervisor (内核代码/堆/栈, 0x83 = P+RW+PS)
    ;   512MB - 1GB: user (用户进程代码区, 0x87 = P+RW+US+PS)
    ;   1GB - 3GB  : supervisor (模块窗口 + VBE LFB, 0x83) —— 内核(running0)读写, 不受 CR0.WP 影响
    ;   3GB - 4GB  : supervisor (VBE LFB 等 MMIO, 0x83)
    cld
    mov dword [0x9000], 0xA007  ; PML4[0] -> PDPT (P+RW+US)
    mov dword [0xA000], 0xB007  ; PDPT[0] -> PD0 (P+RW+US)  0~1GB
    mov dword [0xA008], 0xC007  ; PDPT[1] -> PD1 (P+RW+US)  1~2GB
    mov dword [0xA010], 0xD007  ; PDPT[2] -> PD2 (P+RW+US)  2~3GB
    mov dword [0xA018], 0xE007  ; PDPT[3] -> PD3 (P+RW+US)  3~4GB

    ; PD0 (0~1GB): 前 256 项 supervisor (0-512MB), 后 256 项 user (512MB-1GB)
    mov eax, 0x83               ; P + RW + PS(2MB 页) —— supervisor
    mov edi, 0xB000
    mov ecx, 256                ; 256 项 * 2MB = 512MB supervisor
.build_pd0_kern:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .build_pd0_kern
    mov eax, 0x20000000 | 0x87  ; 512MB 起, P + RW + US + PS —— user
    mov ecx, 256                ; 256 项 * 2MB = 512MB user
.build_pd0_user:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .build_pd0_user

    ; PD1 (1~2GB): 全 supervisor —— 含 CINT 模块窗口(0x40000000~0x80000000), 内核(running0)读写
    mov eax, 0x40000000 | 0x83  ; 1GB 起, supervisor
    mov edi, 0xC000
    mov ecx, 512
.build_pd1:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .build_pd1

    ; PD2 (2~3GB): 全 supervisor —— 含 JVM 模块窗口(0x80000000~0xC0000000)
    mov eax, 0x80000000 | 0x83  ; 2GB 起, supervisor
    mov edi, 0xD000
    mov ecx, 512
.build_pd2:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .build_pd2

    ; PD3 (3~4GB): 全 supervisor (VBE LFB 等 MMIO)
    mov eax, 0xC0000000 | 0x83  ; 3GB 起, supervisor
    mov edi, 0xE000
    mov ecx, 512
.build_pd3:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .build_pd3

    ; ---- 启用长模式 (PAE + LME + PG) ----
    mov eax, cr4
    or eax, 0x20                ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080         ; EFER MSR
    rdmsr
    or eax, 0x100               ; LME (long mode enable)
    xor edx, edx
    wrmsr

    mov eax, 0x9000
    mov cr3, eax                ; 页表基址

    lgdt [gdt64_ptr]            ; 先加载 64 位 GDT
    DBGI '7'

    mov eax, cr0
    or eax, 0x80000001          ; PG | PE
    mov cr0, eax

    jmp 0x08:pm64_start         ; 远跳进入 64 位长模式

; ============================================================
; 64 位长模式: 内核已在 0x100000 (块加载时已就位), 直接跳转
; ============================================================
bits 64
pm64_start:
    mov ax, 0x10                ; 64 位数据段
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; 内核栈保留区 0x1C000000..0x1D000000 (16MB), 位于内核堆上限(0x1C000000)之上、
    ; 用户代码区(0x20000000)之下, 与堆/模块窗口(0x40000000+)均不重叠。
    ; 栈顶 0x1D000000 向下增长。若用 0x90000 会踩低端引导结构导致误判未启动。
    mov rsp, 0x1D000000
    DBGQ '8'

    ; 跳进内核入口 (64 位)
    mov rax, KERNEL_ENTRY
    jmp rax

; ============================================================
; 保护模式小程序: 把 0x10000 处的一整块 (chunk_n 扇区) 拷到
; 0x100000 + copy_pos*512, 然后退回实模式返回调用者。
; 调用约定: 实模式调用 call pm_copy_chunk; 内部自管 RM<->PM 切换。
; ============================================================
bits 16
pm_copy_chunk:
    pushf
    cli
    mov [save_sp], sp
    DBGI 'a'
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp 0x08:.pmc
bits 32
.pmc:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000
    DBGI 'b'
    mov ecx, [chunk_n]
    shl ecx, 7                  ; chunk_n * 512 / 4 = chunk_n * 128
    mov esi, 0x10000
    mov eax, [copy_pos]
    shl eax, 9                  ; * 512
    add eax, 0x100000
    mov edi, eax
    cld
    rep movsd
    DBGI 'c'
    jmp 0x18:.rm16
bits 16
.rm16:
    mov ax, 0x20                ; 16 位数据段: limit 0xFFFF, B=0
    mov ds, ax
    mov es, ax
    mov ss, ax
    movzx esp, word [save_sp]   ; 写满 ESP (清高 16 位), 且 < 64K 符合段限
    DBGI 'd'
    mov eax, cr0
    and eax, ~0x01
    mov cr0, eax
    jmp 0x0000:.rm_real
.rm_real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    DBGI 'e'
    popf                        ; 还原 IF 等标志
    ret

; ============================================================
; 读取子程序: 把 [remaining] 扇区 (磁盘 LBA [dap_lba] 起) 读入 [rd_base] 起
; int13h 扩展读单批最多 0x7F 扇区; 这里每批最多 64 扇区。
; 注意: int 13h 会破坏通用寄存器, 剩余数/已读偏移都保存在内存变量中。
; ============================================================
bits 16
read_batch:
    cmp word [remaining], 0
    je .done
    mov dword [rd_off], 0
.loop:
    mov ax, [remaining]
    cmp ax, 64
    jbe .batch_ok
    mov ax, 64                  ; 本批最多 64 扇区
.batch_ok:
    mov [dap_count], ax
    mov [batch_n], ax           ; 保存本批计划数 (BIOS 可能改写 DAP count)
    mov eax, [rd_off]
    add eax, [rd_base]          ; 目标线性地址 = 基址 + 已读偏移
    mov edx, eax
    shr edx, 4
    mov [dap_segment], dx
    and eax, 0x0F
    mov [dap_offset], ax
    ; 按引导盘类型选择读取方式
    cmp byte [boot_drv], 0x80
    jae .lba_read
.call_chs:
    call chs_read_sectors        ; 软盘/仿真: CHS 读
    jc disk_error
    jmp .advance
.lba_read:
    cmp byte [lba_ok], 0
    jz .call_chs                 ; 不支持扩展读 -> 直接 CHS
    mov si, dap
    mov dl, [boot_drv]
    mov ah, 0x42
    int 0x13
    jnc .advance
    jmp .call_chs                ; LBA 失败 -> 回退 CHS (VMware 兼容性)
.advance:
    movzx eax, word [batch_n]
    imul eax, eax, 512
    add [rd_off], eax
    movzx eax, word [batch_n]
    add dword [dap_lba], eax
    sub [remaining], ax
    jnz .loop
.done:
    ret

; ============================================================
; CHS 读取: 从 [dap_lba] 起读 [batch_n] 扇区到 [dap_offset]:[dap_segment]
; LBA->CHS: sector=(LBA%spt)+1, head=(LBA/spt)%heads, cyl=LBA/(spt*heads)
; 返回 CF=1 出错。
; ============================================================
chs_read_sectors:
    mov eax, [dap_lba]           ; LBA 低 16 位 (内核 < 65536 扇区)
    xor dx, dx
    div word [chs_spt]          ; ax = LBA/spt, dx = LBA%spt
    push dx                     ; sector-1
    xor dx, dx
    div word [chs_heads]        ; ax = cyl, dx = head
    push dx                     ; head
    push ax                     ; cyl
    pop bx                      ; bx = cyl
    mov ch, bl                  ; CH = cyl low 8
    mov ax, bx
    shr ax, 8
    and al, 0x03
    shl al, 6                   ; al = (cyl>>8 & 3) << 6
    push ax
    pop dx                      ; dl = cyl 高 2 位
    pop bx                      ; bx = head
    mov dh, bl                  ; DH = head
    pop bx                      ; bx = sector-1
    inc bx
    mov cl, bl                  ; CL = sector (1-based)
    or cl, dl
    ; 读取 [batch_n] 扇区到 [dap_offset]:[dap_segment]
    mov bx, [dap_offset]
    push es
    mov es, [dap_segment]
    mov ax, [batch_n]
    mov ah, 0x02
    mov dl, [boot_drv]
    int 0x13
    pop es
    ret

; ============================================================
; 辅助
; ============================================================
disk_error:
    DBG 'E'
    call print_err_loader
.hang:
    hlt
    jmp .hang

; 在 VGA 文本模式(0xB8000)打印可见错误, 避免黑屏无提示
print_err_loader:
    push es
    push di
    push si
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov si, err_msg_loader
.pel_loop:
    lodsb
    test al, al
    jz .pel_done
    mov ah, 0x0F        ; 白字黑底
    stosw
    jmp .pel_loop
.pel_done:
    pop si
    pop di
    pop es
    ret

; 探测 int13h 扩展磁盘访问 (AH=0x41): 成功且 bit0 置位 -> lba_ok=1
probe_ext:
    mov byte [lba_ok], 0
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drv]
    int 0x13
    jc .pe_done
    cmp bx, 0xAA55
    jne .pe_done
    test cx, 0x01
    jz .pe_done
    mov byte [lba_ok], 1
.pe_done:
    ret

; ============================================================
; VBE 探测: 候选列表收集 → 排序 → 择优激活
; 遍历 VBE 模式列表, 收集满足 32bpp+LFB+DirectColor+≥640x480 的模式,
; 按 width×height 降序排序 (同像素优先 16:9), 逐个尝试激活, 首个成功写 0x6400.
; 成功: 物理地址 0x6400 写入 VBE 信息块 (magic 'VBE2'), 供 gfx_init() 激活 LFB
; 失败: 不写 magic, gfx_init() 回退 GOP/mode13h
; 内存: 0x7000=VBE Ctrl Info(512B), 0x7400=Mode Info(256B)
;       0x77FE=激活索引, 0x77FF=候选计数, 0x7800=候选数组(32×8B=256B)
; 0x6400 信息块布局 (与 gfx.c vbe_info_t 一致):
;   +0  uint32 magic='VBE2'  +4 width  +8 height  +12 pitch
;   +16 bpp  +20 format(0=RGBX,1=BGRX)  +24 uint64 fb_addr
; ============================================================
vbe_setup:
    pusha
    push es
    ; 1. 获取 VBE 控制器信息 -> 0x7000 (AX=4F00, ES:DI=缓冲)
    xor ax, ax
    mov es, ax
    mov di, 0x7000
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .vs_fail
    ; 检查 'VESA' 签名 (小端: 'V','E','S','A' = 0x41534556)
    cmp dword [0x7000], 0x41534556
    jne .vs_fail
    ; 2. 初始化候选计数
    mov byte [0x77FF], 0
    ; 3. 遍历模式列表, 收集候选 -> 0x7800 数组
    mov si, [0x700E]                     ; 模式列表偏移
    mov ax, [0x7010]                     ; 模式列表段
    mov es, ax                           ; ES:SI = 模式号列表
.vs_collect:
    mov cx, [es:si]
    cmp cx, 0xFFFF
    je .vs_sort                          ; 列表结束, 转排序
    add si, 2
    ; 获取模式信息 -> 0x7400 (AX=4F01, CX=模式号)
    push es
    push si
    xor ax, ax
    mov es, ax
    mov di, 0x7400
    mov ax, 0x4F01
    int 0x10
    pop si
    pop es
    cmp ax, 0x004F
    jne .vs_collect
    ; 校验: ModeAttributes bit0(支持)+bit7(LFB), ≥640x480, 32bpp, DirectColor
    test word [0x7400], 0x0081
    jz .vs_collect
    cmp word [0x7412], 640              ; XRes ≥ 640
    jb .vs_collect
    cmp word [0x7414], 480              ; YRes ≥ 480
    jb .vs_collect
    cmp byte [0x7419], 32               ; BitsPerPixel == 32
    jne .vs_collect
    cmp byte [0x741B], 6                ; MemoryModel == 6 (DirectColor)
    jne .vs_collect
    ; 候选数 < 32 检查
    mov al, [0x77FF]
    cmp al, 32
    jae .vs_collect
    ; 写入候选条目 (8 字节: mode号/width/height/padding)
    movzx bx, al
    shl bx, 3                           ; bx = count × 8
    mov [0x7800 + bx], cx               ; mode 号
    mov ax, [0x7412]
    mov [0x7802 + bx], ax               ; width
    mov ax, [0x7414]
    mov [0x7804 + bx], ax               ; height
    mov word [0x7806 + bx], 0           ; padding
    inc byte [0x77FF]
    jmp .vs_collect

    ; 4. 选择排序: 按 width×height 降序, 同像素优先 16:9
.vs_sort:
    mov cl, [0x77FF]                    ; cl = 候选计数
    cmp cl, 2
    jb .vs_activate                     ; 0 或 1 个候选, 无需排序
    mov ch, 0                           ; ch = i = 0
.vs_sort_outer:
    mov al, cl
    dec al                              ; al = count - 1
    cmp ch, al
    jae .vs_activate                    ; i ≥ count-1, 排序完成
    mov bl, ch                          ; bl = max_idx = i
    mov bh, ch
    inc bh                              ; bh = j = i + 1
.vs_sort_inner:
    cmp bh, cl
    jae .vs_sort_swap_check             ; j ≥ count, 内循环结束
    ; 计算乘积并比较: product(j) vs product(max_idx)
    movzx si, bh
    shl si, 3                           ; si = j × 8
    movzx di, bl
    shl di, 3                           ; di = max_idx × 8
    ; product(j) = width_j × height_j → DX:AX
    mov ax, [0x7802 + si]
    mul word [0x7804 + si]
    push dx
    push ax
    ; product(max) = width_max × height_max → DX:AX
    mov ax, [0x7802 + di]
    mul word [0x7804 + di]
    pop cx                              ; CX = low product(j)
    pop bp                              ; BP = high product(j)
    ; 比较 BP:CX (j) vs DX:AX (max), 32 位无符号
    cmp bp, dx
    ja .vs_j_better
    jb .vs_j_worse
    cmp cx, ax
    ja .vs_j_better
    jb .vs_j_worse
    ; 乘积相等 — 16:9 偏好: |9×width - 16×height| 小者优先
    mov ax, [0x7802 + si]               ; width_j
    mov cx, ax
    shl ax, 3                           ; 8×width
    add ax, cx                          ; 9×width
    mov dx, ax
    mov ax, [0x7804 + si]               ; height_j
    mov cx, ax
    shl ax, 4                           ; 16×height
    sub dx, ax                          ; 9×w_j - 16×h_j
    jns .vs_j_abs
    neg dx
.vs_j_abs:
    mov bp, dx                          ; bp = |9×w_j - 16×h_j|
    mov ax, [0x7802 + di]               ; width_max
    mov cx, ax
    shl ax, 3
    add ax, cx                          ; 9×width_max
    mov dx, ax
    mov ax, [0x7804 + di]               ; height_max
    mov cx, ax
    shl ax, 4                           ; 16×height_max
    sub dx, ax
    jns .vs_max_abs
    neg dx
.vs_max_abs:
    cmp bp, dx                          ; j 偏差 < max 偏差?
    jb .vs_j_better
    jmp .vs_j_worse
.vs_j_better:
    mov bl, bh                          ; max_idx = j
.vs_j_worse:
    inc bh
    jmp .vs_sort_inner
.vs_sort_swap_check:
    cmp bl, ch                          ; max_idx == i?
    je .vs_sort_next                    ; 无需交换
    ; 交换候选 i 与 max_idx (8 字节 = 4 words)
    movzx si, ch
    shl si, 3
    movzx di, bl
    shl di, 3
    mov ax, [0x7800 + si]
    mov cx, [0x7800 + di]
    mov [0x7800 + si], cx
    mov [0x7800 + di], ax
    mov ax, [0x7802 + si]
    mov cx, [0x7802 + di]
    mov [0x7802 + si], cx
    mov [0x7802 + di], ax
    mov ax, [0x7804 + si]
    mov cx, [0x7804 + di]
    mov [0x7804 + si], cx
    mov [0x7804 + di], ax
    mov ax, [0x7806 + si]
    mov cx, [0x7806 + di]
    mov [0x7806 + si], cx
    mov [0x7806 + di], ax
.vs_sort_next:
    inc ch
    jmp .vs_sort_outer

    ; 5. 择优激活: 遍历排序后候选, 逐个 4F02 设置, 首个成功写信息块
.vs_activate:
    cmp byte [0x77FF], 0
    je .vs_fail                          ; 无候选, 回退
    mov byte [0x77FE], 0                 ; 激活索引 = 0
.vs_act_loop:
    mov al, [0x77FE]
    cmp al, [0x77FF]
    jae .vs_fail                         ; 全部候选耗尽
    movzx bx, al
    shl bx, 3
    mov cx, [0x7800 + bx]                ; 模式号
    ; 设置模式: AX=4F02, BX=模式号|0x4000 (bit14=LFB)
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .vs_act_next                     ; 失败, 尝试下一候选
    ; 重新获取模式信息 -> 0x7400
    push cx
    xor ax, ax
    mov es, ax
    mov di, 0x7400
    mov ax, 0x4F01
    int 0x10
    pop cx
    cmp ax, 0x004F
    jne .vs_act_next                     ; 获取信息失败, 尝试下一候选
    ; 写 VBE 信息块到 0x6400
    mov dword [0x6400], 0x32454256       ; magic 'VBE2'
    movzx eax, word [0x7412]             ; width (+18)
    mov [0x6404], eax
    movzx eax, word [0x7414]             ; height (+20)
    mov [0x6408], eax
    movzx eax, word [0x7410]             ; BytesPerScanLine (+16)
    test eax, eax
    jnz .vs_pitch_ok
    movzx eax, word [0x7412]             ; 回退: width × 4
    shl eax, 2
.vs_pitch_ok:
    mov [0x640C], eax                    ; pitch
    movzx eax, byte [0x7419]             ; bpp (+25)
    mov [0x6410], eax
    ; 颜色顺序: RedFieldPosition(+32)==16 -> RGBX(0), 否则 BGRX(1)
    movzx eax, byte [0x7420]             ; RedFieldPosition (+32)
    cmp eax, 16
    je .vs_rgbx
    mov dword [0x6414], 1                ; BGRX
    jmp .vs_fb
.vs_rgbx:
    mov dword [0x6414], 0                ; RGBX
.vs_fb:
    mov eax, [0x7428]                    ; PhysBasePtr (+40)
    mov [0x6418], eax
    mov dword [0x641C], 0                ; 高 32 位 (32 位物理地址)
    jmp .vs_done
.vs_act_next:
    inc byte [0x77FE]
    jmp .vs_act_loop
.vs_fail:
.vs_done:
    pop es
    popa
    ret

err_msg_loader db "FSOS LOADER ERROR: kernel read failed", 0

; COM1(0x3F8) 输出单字符 —— 仅 DEBUG 模式需要
%ifdef DEBUG
com_out:
    mov ah, al                     ; 暂存字符
    push dx
.wait:
    mov dx, 0x3FD                 ; LSR
    in al, dx
    test al, 0x20                 ; THRE
    jz .wait
    mov dx, 0x3F8                 ; THR
    mov al, ah
    out dx, al
    pop dx
    ret
%endif

; ============================================================
; 数据
; ============================================================
boot_drv db 0
lba_ok  db 0          ; 1 = int13h 扩展读(AH=0x42) 可用

; ---- GDT (32 位保护模式用) ----
align 4
gdt:
    dq 0x0000000000000000        ; 0: null
    dq 0x00CF9A000000FFFF        ; 0x08: 32-bit code, base 0, limit 4G
    dq 0x00CF92000000FFFF        ; 0x10: 32-bit data, base 0, limit 4G
    dq 0x00009A000000FFFF        ; 0x18: 16-bit code (D=0), base 0, limit 64K (退回实模式用)
    dq 0x000092000000FFFF        ; 0x20: 16-bit data (B=0), base 0, limit 64K (退回实模式用)
    dq 0x00CF92000000FFFF        ; 0x28: 32-bit data, base 0(运行时改), limit 4G (unreal 高位缓冲)
gdt_ptr:
    dw gdt_ptr - gdt - 1
    dd gdt

; ---- GDT (64 位长模式用) ----
align 4
gdt64:
    dq 0x0000000000000000        ; 0: null
    dq 0x0020980000000000        ; 0x08: 64-bit code (L=1, D=0)
    dq 0x0000920000000000        ; 0x10: 64-bit data
gdt64_ptr:
    dw gdt64_ptr - gdt64 - 1
    dd gdt64

; ---- DAP (Disk Address Packet) ----
align 4
dap:
dap_size    db 0x10
            db 0
dap_count   dw CHUNK_SECTORS
dap_offset  dw 0x0000
dap_segment dw 0x1000          ; 0x10000 >> 4 (读入低缓冲 0x10000)
dap_lba     dd KERNEL_LBA
            dd 0

; 本批计划传输扇区数 (独立于 DAP: 某些 BIOS 会改写 DAP 的 count 字段)
batch_n:
    dw 0

; 剩余待读扇区数 / 目标线性基址 / 已读字节偏移
remaining:
    dw 0
rd_base:
    dd 0
rd_off:
    dd 0

; 块加载状态: 已拷扇区数 / 本块扇区数 / RM<->PM 切换时暂存的实模式 sp
copy_pos:
    dd 0
chunk_n:
    dd 0
save_sp:
    dw 0

; CHS 几何 (软盘/仿真路径用, AH=0x08 查询)
chs_spt:
    dw 18
chs_heads:
    dw 2
