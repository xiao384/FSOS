# verify-term.ps1 - 终端验证 (薄转发壳)
#
# 原实现: 76 行 PowerShell, QEMU 无头 + monitor sendkey 注入 + 串口捕获。
# 现实现: 转发到 fsos.py verify --smoke, 由 verify_boot.py 提供等价冒烟验证。
#
# 用法: .\verify-term.ps1
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$FsosPy = Join-Path $ScriptDir "tools\fsos.py"

# 定位 Python
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

Write-Host "[verify-term] Forwarding to fsos.py verify --smoke ..." -ForegroundColor Cyan
& $py.Source $FsosPy verify --smoke --timeout 30 --no-gdb
exit $LASTEXITCODE
