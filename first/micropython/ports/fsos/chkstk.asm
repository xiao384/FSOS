; chkstk.asm - MinGW 的 __chkstk_ms 栈探测例程 (x86/x64)
;
; MinGW gcc 对栈帧超过 4KB 的函数会自动调用 ___chkstk_ms 进行栈页探测
; (防止跳过守护页直接越界)。裸机内核没有 MSVCRT, 必须自行提供。
; 语义 (与 mingw-w64 runtime 等价):
;   x86 : eax = 需要分配的栈字节数 (cdecl, 从栈返回)
;   x64 : rax = 需要分配的栈字节数 (返回后 rax 不变, 由调用者 sub rsp, rax)
; 探测从调用者栈顶向下逐 4KB 页, 每页写一个字节以触发缺页。
;
; 注: 32/64 位下 gcc 生成的调用符号都是 ___chkstk_ms (3 个下划线),
;     nasm 输出符号名原样保留, 无需区分 MINGW/MINGW64。

%ifidn __OUTPUT_FORMAT__, win64
bits 64
section .text

global ___chkstk_ms
___chkstk_ms:
    push rcx
    push rax
    cmp  rax, 0x1000
    lea  rcx, [rsp + 24]       ; 跳过 2 个 push 与返回地址
    jb   .done
.probe:
    sub  rcx, 0x1000
    sub  rax, 0x1000
    test dword [rcx], 0        ; 触碰页 (只读测试)
    cmp  rax, 0x1000
    ja   .probe
.done:
    sub  rcx, rax
    test dword [rcx], 0
    pop  rax
    pop  rcx
    ret

; 部分 MinGW 版本生成旧名 ___chkstk, 一并提供
global ___chkstk
___chkstk:
    jmp ___chkstk_ms

%else
bits 32
section .text

global ___chkstk_ms
___chkstk_ms:
    push ecx
    push eax
    cmp  eax, 0x1000
    lea  ecx, [esp + 12]       ; 跳过 2 个 push 与返回地址
    jb   .done
.probe:
    sub  ecx, 0x1000
    sub  eax, 0x1000
    test dword [ecx], 0        ; 触碰页 (只读测试)
    cmp  eax, 0x1000
    ja   .probe
.done:
    sub  ecx, eax
    test dword [ecx], 0
    pop  eax
    pop  ecx
    ret

; 部分 MinGW 版本生成旧名 ___chkstk, 一并提供
global ___chkstk
___chkstk:
    jmp ___chkstk_ms
%endif

section .note.GNU-stack noalloc noexec nowrite progbits
