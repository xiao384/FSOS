// vm.h - 简易虚拟机6. (沙盒执行环境)
// 提供栈式字节码执行器, 可在终端用 'vm <file>' 命令运行 .vm 程序。
// 这是一种轻量"虚拟化": 用户程序在沙盒中运行, 无法访问内核内存。
#ifndef VM_H
#define VM_H

#include <stdint.h>

// VM 执行结果
#define VM_OK     0
#define VM_HALT   1
#define VM_ERROR  (-1)

// 运行一段字节码 (返回 VM_OK/VM_HALT/VM_ERROR)
int vm_run(const uint8_t* code, int len);

// 终端命令入口: vm <file>
void vm_command(char* args);

#endif // VM_H