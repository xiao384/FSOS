; setjmp.asm - x86/x64 标准 setjmp/longjmp (供 MicroPython nlrsetjmp.c 使用)
; 与 glibc 语义一致: 保存 callee-saved 寄存器 + 调用者栈指针 + 返回地址。
; jmp_buf 布局 (8 个 machine word):
;   32 位 (dword): [0]=ebx [1]=esi [2]=edi [3]=ebp [4]=esp(调用者) [5]=eip(返回地址) [6],[7] 保留
;   64 位 (qword): [0]=rbx [1]=rbp [2]=r12 [3]=r13 [4]=r14 [5]=r15 [6]=rsp(调用者) [7]=rip(返回地址)
; 目标文件: -f win32 (MinGW, C 符号加前导下划线) 或 -f win64 (MinGW64, 无下划线) 或 -f elf32/-f elf64
;
; 32 位 MinGW/COFF: C 符号 setjmp -> _setjmp (需加下划线)
; 64 位 MinGW/COFF: 符号名原样 setjmp
%ifdef MINGW64
%define SETJMP_NAME setjmp
%define LONGJMP_NAME longjmp
%else
%define SETJMP_NAME _setjmp
%define LONGJMP_NAME _longjmp
%endif

%ifdef MINGW64
; ================= 64 位 (win64, MS x64 调用约定: rcx=env) =================
bits 64
section .text

; int setjmp(jmp_buf env)
global SETJMP_NAME
SETJMP_NAME:
    mov rax, rcx               ; rax = env
    mov [rax + 0],  rbx
    mov [rax + 8],  rbp
    mov [rax + 16], r12
    mov [rax + 24], r13
    mov [rax + 32], r14
    mov [rax + 40], r15
    lea r10, [rsp + 8]         ; 调用者的 rsp (返回地址之上)
    mov [rax + 48], r10
    mov r10, [rsp]             ; 返回地址
    mov [rax + 56], r10
    xor eax, eax               ; 首次返回 0
    ret

; void longjmp(jmp_buf env, int val)   (rcx=env, rdx=val)
global LONGJMP_NAME
LONGJMP_NAME:
    mov rax, rcx               ; rax = env
    mov r10, rdx               ; r10 = val
    test r10, r10
    jnz .val_ok
    mov r10d, 1                ; longjmp(env, 0) 也必须返回非零
.val_ok:
    mov rbx, [rax + 0]
    mov rbp, [rax + 8]
    mov r12, [rax + 16]
    mov r13, [rax + 24]
    mov r14, [rax + 32]
    mov r15, [rax + 40]
    mov rsp, [rax + 48]
    mov r11, [rax + 56]
    mov eax, r10d              ; setjmp 返回 int, 低 32 位即可
    jmp r11                    ; 跳到 setjmp 的调用者, eax = val

%else
; ================= 32 位 (i386 cdecl: env 在 [esp+4]) =================
bits 32
section .text

; int setjmp(jmp_buf env)
global SETJMP_NAME
SETJMP_NAME:
    mov edx, [esp + 4]         ; edx = env
    mov [edx + 0],  ebx
    mov [edx + 4],  esi
    mov [edx + 8],  edi
    mov [edx + 12], ebp
    lea ecx, [esp + 4]         ; 调用者的 esp (返回地址之上)
    mov [edx + 16], ecx
    mov ecx, [esp]             ; 返回地址
    mov [edx + 20], ecx
    xor eax, eax               ; 首次返回 0
    ret

; void longjmp(jmp_buf env, int val)
global LONGJMP_NAME
LONGJMP_NAME:
    mov edx, [esp + 4]         ; edx = env
    mov eax, [esp + 8]         ; eax = val
    test eax, eax
    jnz .val_ok
    mov eax, 1                 ; longjmp(env, 0) 也必须返回非零
.val_ok:
    mov ebx, [edx + 0]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    mov ecx, [edx + 20]
    jmp ecx                    ; 跳到 setjmp 的调用者, eax = val
%endif

section .note.GNU-stack noalloc noexec nowrite progbits
