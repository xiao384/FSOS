# FSOS MicroPython 端口 Makefile
# 复用 MicroPython 官方构建系统(py.mk / mkrules.mk)完成 qstr 生成与编译,
# 产物为静态库 libfsos.a, 由内核的链接步骤合并进内核镜像.
# 使用: mingw32-make -C micropython/ports/fsos libfsos.a
# 架构: make ARCH=x64 (默认, 64 位长模式) 或 ARCH=x86 (32 位)

# 引入顶层构建环境变量 (TOP 会被 mkenv.mk 自动推导为 micropython/)
include ../../py/mkenv.mk

CROSS = 0

# 架构选择: x64 (64 位长模式) / x86 (32 位保护模式)
ARCH ?= x64
ifeq ($(ARCH),x86)
BITS      = 32
NASM_FMT  = win32
NASM_DEF  = -d MINGW
else
BITS      = 64
NASM_FMT  = win64
NASM_DEF  = -d MINGW -d MINGW64
endif

# qstr 端口定义文件
QSTR_DEFS = qstrdefsport.h

MICROPY_ROM_TEXT_COMPRESSION ?= 1

# 引入 py core 的构建定义 (含 qstr/moduledefs/root_pointers 生成规则)
include $(TOP)/py/py.mk

# 内核头文件目录 (vga.h / kb.h / user.h / ata.h / io.h / idt.h)
KERNEL := $(TOP)/..

# 包含路径
INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(TOP)/shared/runtime
INC += -I$(TOP)/shared/libc
INC += -I$(TOP)/shared/readline
INC += -I$(KERNEL)
# 重构后内核头文件分层存放, 必须逐一加入 include 搜索路径,
# 否则 mp_entry.c/krn_bridge.c 找不到 vga.h/user.h/terminal.h 等:
INC += -I$(KERNEL)/kernel/drivers   # vga.h kb.h mouse.h idt.h driver.h ata.h
INC += -I$(KERNEL)/kernel/core      # user.h sysconf.h
INC += -I$(KERNEL)/user             # terminal.h
INC += -I$(KERNEL)/mp_port   # mp_entry.h

# 裸机编译选项 (不启用 -mgeneral-regs-only, 否则 MicroPython 的
# 浮点/x87 代码无法编译; 浮点功能已在 mpconfigport.h 中关闭)
# 显式 -Os + 函数节: 内核受引导扇区 640KB 常规内存限制, 体积必须尽量小
# (--gc-sections 在最终内核链接时启用)
COPT ?= -Os -fno-builtin-memset -fno-builtin-memcpy
CFLAGS += $(INC) -m$(BITS) -ffreestanding -nostdlib -nostartfiles -fno-pie \
           -fno-stack-protector -fno-stack-check -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections \
           -std=c99 -Wall -Wextra -DNDEBUG $(COPT)

# 端口源文件
SRC_C = \
	mp_entry.c \
	mphalport.c \
	krn_bridge.c \
	gen_frozen_fsos.c \
	shared/libc/printf.c \
	shared/libc/string0.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	mingw_compat.c

# setjmp/longjmp 与 chkstk 用 NASM 汇编实现 (x86/x64 双版本)
ASM_SRC = setjmp.asm chkstk.asm
ASM_OBJ = $(addprefix $(BUILD)/, $(ASM_SRC:.asm=.o))

$(BUILD)/%.o: %.asm
	$(ECHO) "NASM $@"
	$(Q)nasm -f $(NASM_FMT) $(NASM_DEF) $< -o $@

# 参与 qstr/模块/根指针收集的文件
SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c \
            mp_entry.c mphalport.c krn_bridge.c gen_frozen_fsos.c

# 对象文件
OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(ASM_OBJ)

# 静态库目标
LIBFSOS = $(BUILD)/libfsos.a
$(LIBFSOS): $(OBJ)
	$(ECHO) "AR $@"
	$(Q)$(AR) rcs $@ $^

all: $(LIBFSOS)

include $(TOP)/py/mkrules.mk
