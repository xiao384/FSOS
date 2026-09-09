; hello.asm - ELF64 测试 (阶段 4: POSIX syscall + spawn/wait)
; 静态链接, 加载到 0x20020000
; pid=3 (初始): 文件读写 + spawn 子进程 + wait
; pid>3 (子进程): 输出标识 + 退出
; System V ABI: rax=号, rdi=a1, rsi=a2, rdx=a3
bits 64
org 0x20020000

ehdr:
    db 0x7F, "ELF"        ; e_ident[0..3]
    db 2                   ; EI_CLASS = ELFCLASS64
    db 1                   ; EI_DATA = ELFDATA2LSB
    db 1                   ; EI_VERSION
    db 0                   ; EI_OSABI
    db 0                   ; EI_ABIVERSION
    db 0,0,0,0,0,0,0       ; padding
    dw 2                   ; e_type = ET_EXEC
    dw 0x3E                ; e_machine = EM_X86_64
    dd 1                   ; e_version
    dq _start              ; e_entry
    dq phdr - $$           ; e_phoff
    dq 0                   ; e_shoff
    dd 0                   ; e_flags
    dw ehdr_size           ; e_ehsize
    dw phdr_size           ; e_phentsize
    dw 1                   ; e_phnum
    dw 0                   ; e_shentsize
    dw 0                   ; e_shnum
    dw 0                   ; e_shstrndx
ehdr_size equ $ - ehdr

phdr:
    dd 1                   ; p_type = PT_LOAD
    dd 5                   ; p_flags = R+X
    dq 0                   ; p_offset
    dq $$                  ; p_vaddr
    dq $$                  ; p_paddr
    dq file_size           ; p_filesz
    dq file_size           ; p_memsz
    dq 0x1000              ; p_align
phdr_size equ $ - phdr

_start:
    ; 1. getpid
    mov rax, 8             ; SYS_GETPID
    syscall
    mov r12, rax           ; r12 = my_pid

    ; 2. 子进程 (pid != 3): 输出标识 + 退出
    cmp r12, 3
    je .parent
    mov rax, 1             ; SYS_WRITE
    lea rdi, [rel child_msg]
    mov rsi, child_msg_len
    syscall
    jmp .exit

.parent:
    ; 3. open("hello.txt") -> fd
    mov rax, 5             ; SYS_OPEN
    lea rdi, [rel fname]
    syscall
    cmp rax, 0
    jl .spawn
    mov r13, rax           ; r13 = fd

    ; 4. read + write (第一次)
    sub rsp, 384
    mov rax, 6             ; SYS_READ
    mov rdi, r13
    mov rsi, rsp
    mov rdx, 64
    syscall
    mov r14, rax           ; r14 = n
    cmp r14, 0
    jle .close
    mov rax, 1             ; SYS_WRITE
    mov rdi, rsp
    mov rsi, r14
    syscall

    ; 5. lseek + read + write (第二次)
    mov rax, 9             ; SYS_LSEEK
    mov rdi, r13
    xor rsi, rsi
    xor rdx, rdx
    syscall
    mov rax, 6             ; SYS_READ
    mov rdi, r13
    mov rsi, rsp
    mov rdx, 64
    syscall
    mov r14, rax
    cmp r14, 0
    jle .close
    mov rax, 1             ; SYS_WRITE
    mov rdi, rsp
    mov rsi, r14
    syscall

    ; 6. list
    mov rax, 11            ; SYS_LIST
    lea rdi, [rsp + 64]
    mov rsi, 256
    syscall
    mov r14, rax
    cmp r14, 0
    jle .close
    mov rax, 1             ; SYS_WRITE
    lea rdi, [rsp + 64]
    mov rsi, r14
    syscall

.close:
    mov rax, 7             ; SYS_CLOSE
    mov rdi, r13
    syscall

.spawn:
    ; 7. spawn("hello.elf") -> child_pid
    mov rax, 12            ; SYS_SPAWN
    lea rdi, [rel elf_name]
    syscall
    cmp rax, 0
    jl .no_spawn
    mov r13, rax           ; r13 = child_pid

    ; 8. wait(child_pid)
    mov rax, 13            ; SYS_WAIT
    mov rdi, r13
    syscall

    ; 9. write("spawn done")
    mov rax, 1             ; SYS_WRITE
    lea rdi, [rel spawn_msg]
    mov rsi, spawn_msg_len
    syscall

.no_spawn:
    ; 10. write(标识)
    mov rax, 1             ; SYS_WRITE
    lea rdi, [rel msg]
    mov rsi, msg_len
    syscall

.exit:
    mov rax, 4             ; SYS_EXIT
    xor rdi, rdi
    syscall
.spin:
    hlt
    jmp .spin

fname:     db "hello.txt", 0
elf_name:  db "hello.elf", 0
msg:       db "Hello World from ELF!", 10
msg_len    equ $ - msg
child_msg: db "Child process running!", 10
child_msg_len equ $ - child_msg
spawn_msg: db "Spawn done!", 10
spawn_msg_len equ $ - spawn_msg

file_size equ $ - $$
