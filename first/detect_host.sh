#!/usr/bin/env bash
# detect_host.sh - POSIX shell 平台事实探测 (cross_platform 任务 1.4)
# 输出 KEY=VALUE 行, 可被 CI 预检 / 薄转发壳 source 使用, 无外部 Python 依赖。
#   bash detect_host.sh     # 打印键值对
#   source detect_host.sh   # 将 OS/NASM_FMT/LD_EMU/TOOL_* 注入当前 shell
set -u

# 仅在被 source 时不覆盖已有值; 直接执行时打印全部
detect_host() {
    local OS=unknown ARCH_TMP NASM_FMT LD_EMU NASM_DBG
    case "$(uname -s)" in
        Linux*)   OS=linux ;;
        Darwin*)  OS=macos ;;
        MINGW*|MSYS*|CYGWIN*) OS=windows ;;
    esac

    case "$(uname -m)" in
        x86_64|amd64) ARCH_TMP=64 ;;
        i386|i486|i586|i686) ARCH_TMP=32 ;;
        *) ARCH_TMP=64 ;;
    esac

    if [ "$OS" = "windows" ]; then
        if [ "$ARCH_TMP" = 32 ]; then NASM_FMT=win32; LD_EMU=i386pe; else NASM_FMT=win64; LD_EMU=i386pep; fi
        NASM_DBG="-g -F cv8"
    else
        if [ "$ARCH_TMP" = 32 ]; then NASM_FMT=elf32; LD_EMU=elf_i386; else NASM_FMT=elf64; LD_EMU=elf_x86_64; fi
        NASM_DBG="-g -F dwarf"
    fi

    OS="$OS" ARCH_BITS="$ARCH_TMP" NASM_FMT="$NASM_FMT" LD_EMU="$LD_EMU" NASM_DBG="$NASM_DBG"
    TOOL_NASM="${TOOL_NASM:-$(command -v nasm 2>/dev/null || true)}"
    TOOL_CC="${TOOL_CC:-$(command -v gcc 2>/dev/null || command -v cc 2>/dev/null || true)}"
    TOOL_QEMU="${TOOL_QEMU:-$(command -v qemu-system-x86_64 2>/dev/null || true)}"

    if [ "${BASH_SOURCE[0]}" != "$0" ]; then
        export OS ARCH_BITS NASM_FMT LD_EMU NASM_DBG TOOL_NASM TOOL_CC TOOL_QEMU
        return 0
    fi
    echo "OS=$OS"
    echo "ARCH_BITS=$ARCH_BITS"
    echo "NASM_FMT=$NASM_FMT"
    echo "LD_EMU=$LD_EMU"
    echo "NASM_DBG=$NASM_DBG"
    echo "TOOL_NASM=${TOOL_NASM:-}"
    echo "TOOL_CC=${TOOL_CC:-}"
    echo "TOOL_QEMU=${TOOL_QEMU:-}"
}

detect_host