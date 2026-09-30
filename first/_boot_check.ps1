# ============================================================
# _boot_check.ps1 - 启动验证 (薄转发壳, 转发到 fsos.py verify)
#
# 原实现: 直接调用 check-iso.py 并匹配 RIP 寄存器值判断是否在内核代码段。
# 现实现: 转发到 fsos.py verify, 经 FSOS_BOOT_OK 哨兵断言判定启动成功。
#
# 用法: .\_boot_check.ps1 [-Timeout <seconds>]
# ============================================================
param(
    [int]$Timeout = 25
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
        "C:\Users\Administrator\AppData\Local\Programs\Python\Python312\python.exe",
        "python3.exe",
        "python.exe"
    )
    foreach ($c in $pyCands) {
        $hit = Get-Command $c -ErrorAction SilentlyContinue
        if ($hit) { $py = $hit; break }
    }
}
if (-not $py) {
    Write-Host "[!] Python not found. Install Python 3.12+ or add to PATH." -ForegroundColor Red
    exit 4
}

Write-Host "[boot_check] Forwarding to fsos.py verify (FSOS_BOOT_OK sentinel)..." -ForegroundColor Cyan
& $py.Source $FsosPy verify --timeout $Timeout --no-gdb
exit $LASTEXITCODE
