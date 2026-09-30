# make_hdd_vm.ps1 - 生成 HDD 启动的 VMware 文件 (薄转发壳)
#
# 原实现: 63 行 PowerShell, qemu-img convert + VMX 写入。
# 现实现: 转发到 fsos.py pack --vmdk, 由 pack 组件提供等价功能。
#
# 用法: .\make_hdd_vm.ps1
# 产出: output/FSOS_HDD.vmdk + FSOS_HDD.vmx (HDD 启动)
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

Write-Host "[make_hdd_vm] Forwarding to fsos.py pack --vmdk ..." -ForegroundColor Cyan
& $py.Source $FsosPy pack --vmdk
exit $LASTEXITCODE
