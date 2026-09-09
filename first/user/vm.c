// vm.c - 简易栈式虚拟机
// 字节码格式: 每条指令 1 字节操作码 + 可选操作数。
// 支持基本算术、栈操作、I/O、控制流, 可执行简单沙盒程序。
//
// 指令集:
//   0x00 HALT      停机
//   0x01 PUSH_I4   压入 4 字节整数 (小端, 紧跟操作码)
//   0x02 PUSH_STR  压入字符串 (1 字节长度 + 数据, 紧跟操作码)
//   0x03 ADD       弹出两个整数, 压入和
//   0x04 SUB       弹出两个整数, 压入差 (a-b, b 先弹)
//   0x05 MUL       弹出两个整数, 压入积
//   0x06 DIV       弹出两个整数, 压入商 (a/b)
//   0x07 DUP       复制栈顶
//   0x08 POP       弹出栈顶
//   0x09 PRINT_I   弹出整数并打印
//   0x0A PRINT_S   弹出字符串并打印
//   0x0B PRINT_NL  打印换行
//   0x0C JMP       无条件跳转 (操作数: 2 字节偏移)
//   0x0D JZ        栈顶为 0 则跳转
//   0x0E JNZ       栈顶非 0 则跳转
//   0x0F CMP_EQ    比较相等
//   0x10 CMP_LT    比较小于
//   0x11 CMP_GT    比较大于
#include "vm.h"
#include "vga.h"
#include "kb.h"
#include "io.h"
#include "gfx.h"
#include <stdint.h>

#define VM_STACK_SIZE 256
#define VM_MAX_STR   256

// 串口输出 (调试)
static void vm_ser(const char* s) {
    while (*s) {
        while ((inb(0x3FD) & 0x20) == 0) {}
        outb(0x3F8, (uint8_t)*s++);
    }
}

// 终端输出 (通过 VGA 文本)
static int g_vm_col = 0;
static int g_vm_row = 0;

static void vm_putc(char c) {
    if (c == '\n') {
        g_vm_col = 0;
        g_vm_row++;
        if (g_vm_row >= 56) g_vm_row = 4;
        return;
    }
    if (g_vm_col >= 78) {
        g_vm_col = 0;
        g_vm_row++;
        if (g_vm_row >= 56) g_vm_row = 4;
    }
    char buf[2] = {c, 0};
    vga_draw_text(4 + g_vm_col * 8, 14 + g_vm_row * 8, buf, COL_LGREEN, COL_BLACK);
    g_vm_col++;
}

static void vm_print_str(const char* s) {
    while (*s) vm_putc(*s++);
}

static void vm_print_int(int32_t v) {
    char buf[16];
    int p = 0;
    if (v < 0) { buf[p++] = '-'; v = -v; }
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n) buf[p++] = tmp[--n];
    buf[p] = 0;
    vm_print_str(buf);
}

int vm_run(const uint8_t* code, int len) {
    int32_t stack[VM_STACK_SIZE];
    int sp = 0;
    int pc = 0;

    g_vm_col = 0;
    g_vm_row = 4;

    while (pc < len) {
        uint8_t op = code[pc++];
        switch (op) {
            case 0x00: // HALT
                return VM_HALT;
            case 0x01: { // PUSH_I4
                if (pc + 4 > len) return VM_ERROR;
                int32_t v = (int32_t)((uint32_t)code[pc] |
                    ((uint32_t)code[pc+1] << 8) |
                    ((uint32_t)code[pc+2] << 16) |
                    ((uint32_t)code[pc+3] << 24));
                pc += 4;
                if (sp >= VM_STACK_SIZE) return VM_ERROR;
                stack[sp++] = v;
                break;
            }
            case 0x02: { // PUSH_STR
                if (pc + 1 > len) return VM_ERROR;
                uint8_t slen = code[pc++];
                if (pc + slen > len) return VM_ERROR;
                // 字符串以指针形式压栈 (用栈顶存索引, 不实用; 改为直接打印)
                vm_print_str((const char*)(code + pc));
                pc += slen;
                break;
            }
            case 0x03: { // ADD
                if (sp < 2) return VM_ERROR;
                stack[sp-2] = stack[sp-2] + stack[sp-1];
                sp--;
                break;
            }
            case 0x04: { // SUB
                if (sp < 2) return VM_ERROR;
                stack[sp-2] = stack[sp-2] - stack[sp-1];
                sp--;
                break;
            }
            case 0x05: { // MUL
                if (sp < 2) return VM_ERROR;
                stack[sp-2] = stack[sp-2] * stack[sp-1];
                sp--;
                break;
            }
            case 0x06: { // DIV
                if (sp < 2) return VM_ERROR;
                if (stack[sp-1] == 0) return VM_ERROR;
                stack[sp-2] = stack[sp-2] / stack[sp-1];
                sp--;
                break;
            }
            case 0x07: { // DUP
                if (sp < 1) return VM_ERROR;
                if (sp >= VM_STACK_SIZE) return VM_ERROR;
                stack[sp] = stack[sp-1];
                sp++;
                break;
            }
            case 0x08: // POP
                if (sp > 0) sp--;
                break;
            case 0x09: { // PRINT_I
                if (sp < 1) return VM_ERROR;
                vm_print_int(stack[--sp]);
                break;
            }
            case 0x0A: { // PRINT_S (栈顶是字符串索引, 这里不实用, 跳过)
                break;
            }
            case 0x0B: // PRINT_NL
                vm_putc('\n');
                break;
            case 0x0C: { // JMP
                if (pc + 2 > len) return VM_ERROR;
                int16_t off = (int16_t)((uint16_t)code[pc] |
                    ((uint16_t)code[pc+1] << 8));
                pc = off;
                break;
            }
            case 0x0D: { // JZ
                if (pc + 2 > len) return VM_ERROR;
                int16_t off = (int16_t)((uint16_t)code[pc] |
                    ((uint16_t)code[pc+1] << 8));
                pc += 2;
                if (sp < 1) return VM_ERROR;
                if (stack[--sp] == 0) pc = off;
                break;
            }
            case 0x0E: { // JNZ
                if (pc + 2 > len) return VM_ERROR;
                int16_t off = (int16_t)((uint16_t)code[pc] |
                    ((uint16_t)code[pc+1] << 8));
                pc += 2;
                if (sp < 1) return VM_ERROR;
                if (stack[--sp] != 0) pc = off;
                break;
            }
            case 0x0F: { // CMP_EQ
                if (sp < 2) return VM_ERROR;
                stack[sp-2] = (stack[sp-2] == stack[sp-1]) ? 1 : 0;
                sp--;
                break;
            }
            case 0x10: { // CMP_LT
                if (sp < 2) return VM_ERROR;
                stack[sp-2] = (stack[sp-2] < stack[sp-1]) ? 1 : 0;
                sp--;
                break;
            }
            case 0x11: { // CMP_GT
                if (sp < 2) return VM_ERROR;
                stack[sp-2] = (stack[sp-2] > stack[sp-1]) ? 1 : 0;
                sp--;
                break;
            }
            default:
                return VM_ERROR;
        }
    }
    return VM_OK;
}

// 内置示例程序在 vm_command() 中内联构建

void vm_command(char* args) {
    (void)args;
    vga_clear(COL_BLACK);
    vga_draw_text(4, 4, "FSOS Virtual Machine", COL_YELLOW, COL_BLACK);
    vga_draw_text(4, 14, "Running demo program...", COL_LGRAY, COL_BLACK);

    // 简单演示: 打印 "Hello from VM!" 和计算 6*7=42
    uint8_t prog[] = {
        0x02, 15, 'H','e','l','l','o',' ','f','r','o','m',' ','V','M','!',
        0x0B,            // print newline
        0x02, 4, '6','*','7','=',
        0x01, 6,0,0,0,   // push 6
        0x01, 7,0,0,0,   // push 7
        0x05,            // mul
        0x09,            // print int
        0x0B,            // print newline
        0x02, 7, 'V','M',' ','O','K',
        0x0B,            // print newline
        0x00,            // halt
    };

    int r = vm_run(prog, sizeof(prog));
    vm_ser("VM result: ");
    vm_ser(r == VM_HALT ? "HALT\n" : (r == VM_OK ? "OK\n" : "ERROR\n"));

    vga_draw_text(4, 14 + 12 * 8, "VM finished. Press any key to return.", COL_LGRAY, COL_BLACK);
    gfx_flip();
    kb_wait();
}