# ============================================================
# first project - MinGW 构建脚本 (薄转发壳, 转发到 fsos.py build)
#
# 原实现: 465 行 PowerShell 构建编排 (NASM + GCC + LD + objcopy + 模块链接)。
# 现实现: 转发到 fsos.py build, 由 kernel_build.py 提供等价功能。
#
# 产出: kernel.exe (PE 参考) + kernel.bin (干净扁平内核)
#       + boot.bin (引导扇区) + image.img (可启动软/硬盘镜像)
#       + CINT.MOD / JVM.MOD (解释器模块) + FSOS.iso (El Torito)
#
# 参数映射 (与 fsos.py build 一一对应):
#   -Arch x64 (默认) : 64 位长模式内核   -> --arch x64
#   -Arch x86        : 32 位保护模式内核  -> --arch x86
#   -Clean           : 清理后全量构建     -> --clean
#   -Run             : 构建后启动 QEMU    -> --run
#   -WithPython      : 链接 MicroPython   -> --with-python
#
# 注意: PowerShell 5.1 的 -Clean 会触发 Remove-Item bug,
#       建议手动清理目录后用不带 -Clean 的构建。
# ============================================================
param(
    [ValidateSet("x64","x86")]
    [string]$Arch = "x64",
    [switch]$Clean,
    [switch]$Run,
    [switch]$WithPython
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

# 构造 fsos.py 参数: --platform 是全局参数, 必须置于子命令 build 之前
$cmdArgs = @("--platform", "win32", "build", "--arch", $Arch)
if ($Clean)     { $cmdArgs += "--clean" }
if ($Run)       { $cmdArgs += "--run" }
if ($WithPython) { $cmdArgs += "--with-python" }

Write-Host ("[build-mingw] Forwarding to fsos.py {0} ..." -f ($cmdArgs -join ' ')) -ForegroundColor Cyan
& $py.Source $FsosPy @cmdArgs
exit $LASTEXITCODE
