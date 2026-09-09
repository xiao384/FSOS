# CMake 工具链文件 —— i686-elf 交叉编译
# 使用前请确保已安装 i686-elf-gcc 工具链
#
# 获取方式 (推荐用 WSL 或 MSYS2):
#   Ubuntu/Debian: sudo apt install gcc-i686-linux-gnu binutils-i686-linux-gnu
#   Arch:          sudo pacman -S i686-elf-gcc
#   或从源码编译:   https://wiki.osdev.org/GCC_Cross-Compiler

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  i686)

# 编译器
set(CMAKE_C_COMPILER        i686-elf-gcc)
set(CMAKE_ASM_NASM_COMPILER nasm)

# 链接器
set(CMAKE_LINKER            i686-elf-ld)

# 不尝试运行编译后的程序
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 搜索路径
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
