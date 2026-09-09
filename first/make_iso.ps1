# ============================================================
# 生成 GRUB2 可启动 ISO (交叉编译 ELF 路径, "标准"引导方式)
# 前置依赖 (本机 MinGW 默认没有, 需另行安装):
#   - i686-elf 交叉工具链: i686-elf-gcc / i686-elf-ld
#       MSYS2: pacman -S mingw-w64-i686-gcc  (或 WSL: apt install gcc-i686-linux-gnu)
#       或从源码编译: https://wiki.osdev.org/GCC_Cross-Compiler
#   - grub-mkrescue (GRUB 工具): 安装 GRUB for your platform
#       Windows: 随 GRUB2Win / MSYS2 mingw-w64-x86_64-grub 提供
#       Linux/WSL: apt install grub-pc-bin xorriso
# 用法: .\make_iso.ps1
#   1) 用 i686-elf 工具链交叉编译出 kernel.elf
#   2) 组装 iso/ 目录 (boot/kernel.elf + boot/grub/grub.cfg)
#   3) grub-mkrescue 产出 pixel.iso
#   4) 运行: qemu-system-i386 -cdrom pixel.iso
# ============================================================
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$isoDir    = Join-Path $ScriptDir "iso"
$bootDir   = Join-Path $isoDir "boot"
$grubDir   = Join-Path $bootDir "grub"
$elf       = Join-Path $OutputDir "kernel.elf"
$OutputDir = Join-Path $ScriptDir "output"

# 1. 交叉编译 ELF 内核
if (-not (Get-Command i686-elf-gcc -ErrorAction SilentlyContinue)) {
    Write-Host "[!] i686-elf-gcc not found. Install the cross toolchain first." -ForegroundColor Yellow
    Write-Host "    See header comments for install commands." -ForegroundColor Gray
    exit 1
}
Write-Host "[X] Cross-compiling kernel.elf ..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $bootDir | Out-Null
New-Item -ItemType Directory -Force -Path $grubDir | Out-Null
& nasm -f elf32 -i $ScriptDir -o "$ScriptDir\build-cross\start.o" "$ScriptDir\start.asm"
& i686-elf-gcc -m32 -ffreestanding -nostdlib -nostartfiles -fno-pie -fno-stack-protector -mgeneral-regs-only -Wall -Wextra -c -o "$ScriptDir\build-cross\kernel.o" "$ScriptDir\kernel.c"
& i686-elf-ld -m elf_i386 -T "$ScriptDir\linker.ld" -nostdlib -nostartfiles -o $elf "$ScriptDir\build-cross\start.o" "$ScriptDir\build-cross\kernel.o"
if ($LASTEXITCODE -ne 0) { throw "cross link failed" }

# 2. 组装 iso 目录
Copy-Item "$ScriptDir\grub.cfg" $grubDir -Force
Copy-Item $elf $bootDir -Force

# 3. grub-mkrescue
$mkrescue = Get-Command grub-mkrescue -ErrorAction SilentlyContinue
if (-not $mkrescue) {
    Write-Host "[!] grub-mkrescue not found. Install GRUB tools first." -ForegroundColor Yellow
    exit 1
}
$iso = Join-Path $ScriptDir "pixel.iso"
& $mkrescue.Path -o $iso $isoDir
if ($LASTEXITCODE -ne 0) { throw "grub-mkrescue failed" }
Write-Host ("[OK] Built {0}" -f $iso) -ForegroundColor Green
