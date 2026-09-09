// setjmp.h - 裸机 x86 的极简 setjmp/longjmp (供 nlrsetjmp.c 使用)
//
// jmp_buf 布局由 setjmp.asm 定义 (每槽一个 machine word):
//   32 位 (8 dword): ebx, esi, edi, ebp, esp, eip, +pad, +pad
//   64 位 (8 qword): rbx, rbp, r12, r13, r14, r15, rsp, rip
// 前 8 槽被 setjmp/longjmp 使用, 其余为 padding。
//
// 注意: 64 位下 jmp_buf 必须足够大 (128B), 否则 nlr_buf_t 太小,
//       gcc 会把局部 nlr 缓冲布局到函数 prologue 的 push 区上方,
//       setjmp 写 jmpbuf 会覆盖 prologue 保存的 callee-saved 寄存器,
//       导致函数返回时恢复错误寄存器 (#GP)。
#ifndef SETJMP_H
#define SETJMP_H

typedef unsigned long jmp_buf[16];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif // SETJMP_H
