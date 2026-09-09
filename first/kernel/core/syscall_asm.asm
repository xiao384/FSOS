; syscall_asm.asm - syscall 入口 (64 位长模式)
;
; 用户态执行 syscall 指令后 CPU 跳到此处:
;   进入时: rax = 系统调用号 (System V)
;           rdi/rsi = 前两个参数
;           rcx  = 用户返回 RIP (syscall 指令自动保存)
;           r11  = 用户 RFLAGS
;           rsp  = 用户栈 (syscall 不自动切换栈!)
;   因此这里必须手动切到内核栈, 返回前再切回并用 sysretq 返回 ring3。
;
; 参数映射 (System V -> MS x64 ABI):
;   num = rax -> rcx    a1 = rdi -> rdx    a2 = rsi -> r8
bits 64
section .text

    extern handle_syscall
    extern g_syscall_kernel_rsp

    global syscall_entry
syscall_entry:
    ; 1. 切到内核栈, 在内核栈上保存用户栈 (不用全局变量, 避免多进程覆盖)
    mov r10, rsp                      ; r10 = 用户栈 (r10 在参数映射中不用)
    mov rsp, [rel g_syscall_kernel_rsp]  ; 切到当前进程的内核栈
    push r10                          ; [rsp+0]  保存用户栈
    push r11                          ; [rsp+8]  保存 RFLAGS (sysret 用)
    push rcx                          ; [rsp+16] 保存返回 RIP (sysret 用)
    push r9                           ; [rsp+24] 第 6 参数 (Linux r9)
    push r8                           ; [rsp+32] 第 5 参数 (Linux r8)

    ; 2. 参数映射 (System V -> MS x64 ABI):
    ;   num = rax -> rcx    a1 = rdi -> rdx    a2 = rsi -> r8    a3 = rdx -> r9
    mov r9,  rdx        ; a3 -> r9  (MS 第4参数)
    mov r8,  rsi        ; a2 -> r8  (MS 第3参数)
    mov rdx, rdi        ; a1 -> rdx (MS 第2参数)
    mov rcx, rax        ; num -> rcx (MS 第1参数)

    ; 3. Linux 第 5/6 参数 (r8,r9) 经栈传递 (MS ABI: a5=[rsp+32], a6=[rsp+40])
    sub rsp, 48                         ; shadow(32) + a5(8) + a6(8)
    mov rax, [rsp+48]                   ; 保存的 r8 (=5th arg 来源)
    mov [rsp+32], rax
    mov rax, [rsp+56]                   ; 保存的 r9 (=6th arg 来源)
    mov [rsp+40], rax

    ; 4. 调用 C 分发函数 (handle_syscall(num,a1,a2,a3,a4,a5,a6))
    call handle_syscall
    add rsp, 48

    ; 5. 恢复 (rax 已是返回值)
    pop r8                             ; 恢复用户 r8
    pop r9                             ; 恢复用户 r9
    pop rcx                            ; 返回 RIP
    pop r11                            ; 返回 RFLAGS
    pop r10                            ; 恢复用户栈指针
    mov rsp, r10                       ; 切回用户栈
    o64 sysret

; ============================================================
; 位置无关的用户态代码模板 (ring3 执行)
; 复制到 USER_CODE_BASE (0x20020000) 后执行, 不依赖任何绝对地址。
; 触发 SYS_WRITE_MSG(0) 输出消息后 pause 死循环。
; ============================================================
align 16
global user_code_template
user_code_template:
    xor rax, rax            ; num = 0 (SYS_NULL)
    syscall
.spin:
    pause
    jmp .spin
global user_code_template_end
user_code_template_end:

; ============================================================
; enter_ring3_test - 从 ring0 切换到 ring3 执行用户态代码
; 1. 把 user_code_template 复制到 USER_CODE_BASE (0x20020000, 用户空间)
; 2. iretq 跳到 USER_CODE_BASE, 栈设为 USER_STACK_TOP (0x20010000)
; ============================================================
global enter_ring3_test
enter_ring3_test:
    ; 1. 直接写机器码到 USER_CODE_BASE (0x20000000, 2MB 对齐)
    ;    xor rdi,rdi     = 48 31 FF        (a1 = 0, 消息索引)
    ;    mov rax,3       = 48 C7 C0 03 00 00 00  (SYS_WRITE_MSG)
    ;    syscall         = 0F 05
    ;    pause           = F3 90
    ;    jmp -2          = EB FC
    mov rdi, 0x20000000
    mov byte [rdi+0],  0x48
    mov byte [rdi+1],  0x31
    mov byte [rdi+2],  0xFF
    mov byte [rdi+3],  0x48
    mov byte [rdi+4],  0xC7
    mov byte [rdi+5],  0xC0
    mov byte [rdi+6],  0x03
    mov byte [rdi+7],  0x00
    mov byte [rdi+8],  0x00
    mov byte [rdi+9],  0x00
    mov byte [rdi+10], 0x0F
    mov byte [rdi+11], 0x05
    mov byte [rdi+12], 0xF3
    mov byte [rdi+13], 0x90
    mov byte [rdi+14], 0xEB
    mov byte [rdi+15], 0xFC

    ; 2. 构造 iretq 栈帧, 跳到用户代码
    push 0x1B               ; SS = 用户数据段 (0x18 | 3)
    push qword 0x20100000   ; RSP = 用户栈 (512MB+1MB)
    push qword 0x202        ; RFLAGS = IF=1
    push 0x23               ; CS = 用户代码段 (0x20 | 3)
    push qword 0x20000000   ; RIP = USER_CODE_BASE (2MB 对齐)
    iretq