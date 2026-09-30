# ============================================================
# pack_iso.ps1 - 生成可启动 ISO (薄转发壳, 转发到 fsos.py pack --iso)
#
# 原实现: 359 行纯 PowerShell 手写 ISO 9660 + El Torito No-Emulation 引导。
# 现实现: 转发到 fsos.py pack --iso, 由 build_iso.py 提供等价功能。
#
# ISO 布局 (逻辑扇区 2048 字节):
#   0x000-0x00F  系统区(16 扇区)
#   0x010        PVD (Primary Volume Descriptor)
#   0x011        El Torito Boot Record
#   0x012        Volume Descriptor Terminator
#   0x013-0x014  Boot Catalog (2 扇区)
#   0x015        Path Table (Type L)
#   0x016        Path Table (Type M)
#   0x017        Root Directory
#   0x018        iso_boot.bin (引导加载器, 1 扇区)
#   0x019        KERNEL.BIN
#   0x019+N      BOOT.BIN
#   0x019+N+M    IMAGE.IMG
#
# 用法: .\pack_iso.ps1 [-TargetDir <dir>]   (默认 ../iso, 由 fsos.py 处理)
# ============================================================
param(
    [string]$TargetDir = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$FsosPy = Join-Path $ScriptDir "tools\fsos.py"

# 定位 Python: 优先 PATH, 回退到已知安装位置
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) {
    $pyCands = @(
        "C:\Users\Administrator\AppData\Local\Programs\Python\Python314\python.exe",
        "C:\Users\Administrator\AppData\Local\Programs\Python\Python313\python.exe",
        "C:\Users\Administrator\AppData\Local\Programs\Python\Python312\python.exe"
    )
    foreach ($c in $pyCands) {
        if (Test-Path $c) { $py = [PSCustomObject]@{ Source = $c }; break }
    }
}
if (-not $py) {
    Write-Host "[!] Python not found. Install Python 3.12+ or add to PATH." -ForegroundColor Red
    exit 4
}

Write-Host "[pack_iso] Forwarding to fsos.py pack --iso ..." -ForegroundColor Cyan
if ($TargetDir) {
    Write-Host "[pack_iso] Note: -TargetDir is handled by fsos.py default (../iso)" -ForegroundColor Gray
}
& $py.Source $FsosPy pack --iso
exit $LASTEXITCODE
