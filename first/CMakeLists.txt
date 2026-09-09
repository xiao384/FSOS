# first 项目 CMake 构建配置 (x64 长模式内核, 与 build-mingw.ps1 参数链路一致)
# 用法:
#   CLion / 命令行使用 MinGW 工具链 (gcc/g++/nasm):
#     cmake -B build -G "MinGW Makefiles"
#   build-mingw.ps1 是官方完整构建 (含模块/镜像/ISO), CMake 仅用于 IDE 索引/编译/调试。

cmake_minimum_required(VERSION 3.20)
project(first_kernel C CXX ASM_NASM)

# ============================================================
# 汇编器配置 (NASM, win64 COFF, 匹配 build-mingw.ps1 的 -f win64 -d MINGW -d MINGW64)
# ============================================================
set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
set(CMAKE_ASM_NASM_FLAGS "-f win64 -g -F cv8 -d MINGW -d MINGW64")
set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS asm)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/kernel/core
    ${CMAKE_SOURCE_DIR}/kernel/drivers
    ${CMAKE_SOURCE_DIR}/user
    ${CMAKE_SOURCE_DIR}/mp_port
)

# ============================================================
# 编译器配置 (裸机 64 位内核, freestanding; 参数对齐 build-mingw.ps1)
# ============================================================
set(KERNEL_COMMON_FLAGS
    -m64 -Os -ffreestanding -nostdlib -nostartfiles
    -fno-pie -fno-stack-protector -mgeneral-regs-only
    -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections
    -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${KERNEL_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${KERNEL_COMMON_FLAGS} -fno-rtti -fno-exceptions")

# ============================================================
# 链接器配置 (g++ 驱动带 libgcc, PE x86_64, 内核链接脚本)
# ============================================================
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--image-base,0 -Wl,-mi386pep -T ${CMAKE_SOURCE_DIR}/linker.ld -nostdlib")

# ============================================================
# 源文件: 按真实目录扫描, 与 build-mingw.ps1 的目录清单保持一致
# (kernel/core + kernel/drivers + user 各层 *.c/*.cpp + 三个 asm + mp 桩)
# ============================================================
file(GLOB KERNEL_C_SRCS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/kernel/core/*.c
    ${CMAKE_SOURCE_DIR}/kernel/drivers/*.c
    ${CMAKE_SOURCE_DIR}/user/*.c)
file(GLOB KERNEL_CPP_SRCS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/user/*.cpp)

add_executable(kernel
    ${CMAKE_SOURCE_DIR}/kernel/core/start.asm
    ${CMAKE_SOURCE_DIR}/kernel/core/idt_asm.asm
    ${CMAKE_SOURCE_DIR}/kernel/core/syscall_asm.asm
    ${KERNEL_C_SRCS}
    ${KERNEL_CPP_SRCS}
    ${CMAKE_SOURCE_DIR}/mp_port/mp_stub.c
)

# MicroPython 默认不参与 CMake 构建 (与 build-mingw.ps1 默认桩路径一致);
# 完整 Python 构建请使用 build-mingw.ps1 -WithPython。

# 输出 kernel.exe 到 first/output
set_target_properties(kernel PROPERTIES
    OUTPUT_NAME kernel
    SUFFIX ".exe"
    LINKER_LANGUAGE CXX
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/output"
)