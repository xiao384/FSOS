# make_uefi_vm_auto.ps1 - 生成 UEFI 启动的 FSOS VMware 文件 (薄转发壳)
#
# 原实现: 96 行 PowerShell, 编译 BOOTX64.EFI + make_uefi_disk.py + qemu-img + VMX。
# 现实现: 转发到 fsos.py pack --uefi --vmdk, 由 pack_uefi.py 提供等价功能。
#
# 用法: .\make_uefi_vm_auto.ps1
# 产出: output/Auto/FSOS_UEFI.vmdk + FSOS_UEFI.vmx (UEFI 固件)
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

Write-Host "[make_uefi_vm_auto] Forwarding to fsos.py pack --uefi --vmdk ..." -ForegroundColor Cyan
& $py.Source $FsosPy pack --uefi --vmdk
exit $LASTEXITCODE
