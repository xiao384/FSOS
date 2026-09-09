; idt_asm.asm - ISR / IRQ 桩 (64 位长模式)
;
; 符号约定: x86_64-w64-mingw32 的 C 符号不加前导下划线,
;           ELF64 也不加, 故 IDT_DISPATCH 直接用 idt_dispatch。
bits 64
section .text

extern idt_dispatch
extern g_exc_cr2
extern sched_tick_isr          ; 阶段 2: 调度钩子

; 宏: 无错误码的异常/IRQ (64 位异常栈: [RIP, CS, RFLAGS, RSP, SS])
; 入栈后: [vector, dummy, RIP, CS, RFLAGS, RSP, SS]
%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0          ; dummy error code
    push %1         ; vector
    jmp isr_common
%endmacro

; 宏: 有错误码的异常
%macro ISR_ERR 1
global isr%1
isr%1:
    push %1         ; vector (error code 已在栈上, 位于其下)
    jmp isr_common
%endmacro

; 宏: IRQ (无错误码)
%macro IRQ 1
global irq%1
irq%1:
    push 0          ; dummy error code
    push %1 + 32    ; vector = irq + 32
    jmp isr_common
%endmacro

; 异常 0-31
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; IRQ 0-15
IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15

isr_common:
    ; 保存全部通用寄存器 (15 个)
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 栈: [r15..rax (15*8=120), vector, dummy/err, RIP, CS, RFLAGS, RSP, SS]
    ; vector 在 [rsp+120], RIP 在 [rsp+136], CS 在 [rsp+144], RFLAGS 在 [rsp+152]
    ; 注意: MinGW x64 使用 MS x64 ABI, 前 4 参数在 rcx/rdx/r8/r9 (不是 System V 的 rdi/rsi/rdx/rcx!)
    mov rcx, [rsp + 120]   ; 1st arg: vector
    mov rdx, [rsp + 136]   ; 2nd arg: eip (RIP)
    mov r8,  [rsp + 144]   ; 3rd arg: cs
    mov r9,  [rsp + 152]   ; 4th arg: eflags
    ; 16 字节栈对齐 + 32 字节 shadow space (MS x64 ABI 要求)
    mov rbp, rsp
    ; 捕获 #PF 故障线性地址 (CR2), 供 exception_dump 打印
    mov rax, cr2
    mov [rel g_exc_cr2], rax
    and rsp, ~0xF
    sub rsp, 32
    call idt_dispatch
    mov rsp, rbp

    ; ---- 阶段 2 调度钩子 ----
    ; sched_tick_isr(cur_rsp) -> 新栈帧 rsp (0=不切换)
    ; 此时 rsp=rbp=中断栈帧指针, 保存到 rcx 作参数
    mov rcx, rbp              ; 1st arg (MS ABI) = 当前中断栈帧指针
    and rsp, -16              ; 16 字节对齐
    sub rsp, 32               ; shadow space
    call sched_tick_isr
    mov rsp, rbp              ; 先恢复到当前中断栈帧
    test rax, rax
    jz  .no_sched
    mov rsp, rax              ; 切到新线程的栈帧
.no_sched:
    ; ---- 调度钩子结束 ----

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    add rsp, 16      ; 清理 vector + dummy/error code
    iretq
