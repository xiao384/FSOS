#!/usr/bin/env python3
"""build_config.py - FSOS 跨平台构建全局常量表 (cross_platform 任务 1.2)

集中定义最低工具版本要求、产物守卫、跨平台一致编译标志集、QEMU 候选目录等,
供 checkenv / build / pack / verify 全部组件读取, 避免数字/标志散落硬编码。
"""
# ---------------------------------------------------------------------------
# 最低版本要求 (checkenv 自检阈值)
# ---------------------------------------------------------------------------
NASM_MIN = "2.14"
GCC_MIN = "8"
CLANG_MIN = "12"
PYTHON_MIN = "3.8"
QEMU_MIN = "6.0"

# ---------------------------------------------------------------------------
# 产物守卫 (与 lang_runtime 回归基线一致)
# ---------------------------------------------------------------------------
MOD_MAX_BYTES = 2 * 1024 * 1024      # 解释器模块上限 2MB (防 GB 级空洞回归)
BOOT_SECTOR_BYTES = 512              # 引导扇区固定 512B
KERNEL_MAX_WARN = 5991               # 内核扇区上限 (layout.h KERNEL_MAX_SECTORS=6000-9, 仅提示)

# ---------------------------------------------------------------------------
# 跨平台一致编译标志集: 三平台完全相同 (仅工具名/路径随平台变化)
# 取值对齐 build-mingw.ps1 的 -m64 链路 (84-100 行), 供 kernel_build.py 拼接
# ---------------------------------------------------------------------------
flags_as = "-g"                                   # NASM 调试; 具体 -F 由注入层给
flags_common = (
    "-ffreestanding -nostdlib -nostartfiles "
    "-fno-pie -fno-stack-protector -mgeneral-regs-only "
    "-fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections "
    "-finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra"
)
flags_arch = {64: "-m64 -Os", 32: "-m32 -Os"}
flags_cxx_extra = "-fno-rtti -fno-exceptions"
flags_link_common = ("-nostdlib -Wl,--gc-sections")

# 允许 MicroPython 独立构建时的遥距变量 (实际路径由 LayoutMap / 目录扫描决定)
ifndef_guards = {
    "DISK_SECTORS": "layout.h",
}

# ---------------------------------------------------------------------------
# 宿主工具候选 (resolve_tool 建议候选)
# ---------------------------------------------------------------------------
QEMU_NAMES = ["qemu-system-x86_64.exe", "qemu-system-x86_64"]
NASM_NAMES = ["nasm", "nasm.exe"]
CC_NAMES = ["gcc", "cc", "x86_64-linux-gnu-gcc", "clang", "gcc.exe"]
CXX_NAMES = ["g++", "c++", "clang++", "g++.exe"]
OBJCOPY_NAMES = ["objcopy", "x86_64-linux-gnu-objcopy", "objcopy.exe"]

# 镜像内文件名大小写规范 (ISO/UEFI 侧 FAT 语义统一)
ISO_NAME_IMAGE = "image.img"
ISO_NAME_KERNEL = "kernel.bin"
ISO_NAME_BOOT = "boot.bin"
ISO_NAME_LOADER = "loader.bin"


class BuildConfig:
    """常量门面: 组件可 from build_config import BuildConfig 后统一取值。"""

    NASM_MIN = NASM_MIN
    GCC_MIN = GCC_MIN
    CLANG_MIN = CLANG_MIN
    PYTHON_MIN = PYTHON_MIN
    QEMU_MIN = QEMU_MIN

    MOD_MAX_BYTES = MOD_MAX_BYTES
    BOOT_SECTOR_BYTES = BOOT_SECTOR_BYTES
    KERNEL_MAX_WARN = KERNEL_MAX_WARN

    flags_common = flags_common
    flags_arch = flags_arch
    flags_cxx_extra = flags_cxx_extra

    QEMU_NAMES = QEMU_NAMES
    NASM_NAMES = NASM_NAMES
    CC_NAMES = CC_NAMES
    CXX_NAMES = CXX_NAMES
    OBJCOPY_NAMES = OBJCOPY_NAMES

    @staticmethod
    def compiler_flags(arch_bits):
        return ("%s %s" % (flags_arch.get(arch_bits, flags_arch[64]), flags_common)).split()


if __name__ == "__main__":
    # 自检: 打印标志集合供人工比对三平台一致性
    import json
    print(json.dumps({
        "min": {"nasm": NASM_MIN, "gcc": GCC_MIN, "clang": CLANG_MIN,
                "python": PYTHON_MIN, "qemu": QEMU_MIN},
        "mod_max_bytes": MOD_MAX_BYTES,
        "flags_arch64": BuildConfig.compiler_flags(64),
        "flags_arch32": BuildConfig.compiler_flags(32),
    }, indent=2))