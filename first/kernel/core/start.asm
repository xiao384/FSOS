; start.asm - 64 位长模式内核入口
;
; 构建方式:
;   MinGW (PE32+):  nasm -f win64 -d MINGW start.asm -o start.obj
;   ELF (GRUB):     nasm -f elf64 start.asm -o start.o  (暂未启用)
;
; 说明: x86_64-w64-mingw32 的 C 符号在 64 位下不加前导下划线
;       (与 32 位 MinGW 不同), 故 MINGW64 时直接使用 C 符号名。
bits 64

; ---- 符号命名兼容 ------------------------------------------------
; 32 位 MinGW 在 C 符号前加下划线; 64 位 MinGW / ELF 不加
%ifdef MINGW
  %ifdef MINGW64
    %define KERNEL_MAIN  kernel_main
  %else
    %define KERNEL_MAIN  _kernel_main
  %endif
%else
  %define KERNEL_MAIN  kernel_main
%endif

section .multiboot
align 8
mb2_header_start:
    dd 0xe85250d6                ; 魔数
    dd 0                         ; 架构: 0 = x86 (GRUB 会进入保护模式, 本内核由自定义引导直接进长模式)
    dd mb2_header_end - mb2_header_start
    dd -(0xe85250d6 + 0 + (mb2_header_end - mb2_header_start)) ; 校验和
    dw 0    ; 结束 tag (type=0, flags=0)
    dw 0
    dd 8
mb2_header_end:

section .text
global _start
extern KERNEL_MAIN               ; C 入口

_start:
    ; 此时由自定义引导扇区进入 64 位长模式
    mov rsp, stack_top

    ; 清零 BSS 段 —— 用 stosq 按 8 字节清零
    extern __bss_start, __bss_end
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    shr rcx, 3                   ; /8, 得 qword 个数
    xor rax, rax
    rep stosq
    ; 处理剩余不足 8 字节的尾部
    mov rcx, __bss_end
    sub rcx, rdi
    and rcx, 7
    rep stosb

    ; 16 字节栈对齐 (System V / MS x64 ABI 要求)
    and rsp, ~0xF

    ; 启用 SSE/FPU: MicroPython 编译产物含 SSE 指令,
    ; 若 CR4.OSFXSR 未置位, 执行 SSE 指令会触发 #UD 异常。
    ;   CR4.OSFXSR = bit 9, CR4.OSXMMEXCPT = bit 10
    ;   CR0.MP = bit 1, CR0.EM = bit 2 (清 0)
    mov rax, cr0
    and rax, 0xFFFFFFFFFFFFFFF9  ; EM=0, MP=0
    or  rax, 0x2                 ; MP=1
    mov cr0, rax
    mov rax, cr4
    or  rax, 0x600               ; OSFXSR | OSXMMEXCPT
    mov cr4, rax
    ; 清 XMM 寄存器
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    pxor xmm6, xmm6
    pxor xmm7, xmm7
    pxor xmm8, xmm8
    pxor xmm9, xmm9
    pxor xmm10, xmm10
    pxor xmm11, xmm11
    pxor xmm12, xmm12
    pxor xmm13, xmm13
    pxor xmm14, xmm14
    pxor xmm15, xmm15

    ; 调用 C 内核主函数 (System V x64: 第一个参数在 rdi)
    ; 自定义引导路径没有 multiboot 信息, 传 0
    xor edi, edi
    call KERNEL_MAIN
    ; 如果 kernel_main 返回，停机
    cli
    hlt

section .bss
align 16
stack_bottom:
    resb 65536   ; 64 KB 内核栈 (GUI 调用较深)
stack_top:
; 供 C 侧 (mp_entry.c 的 GC 栈扫描) 引用内核栈顶
global stack_top
