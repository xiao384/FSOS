# ============================================================
# 运行内核镜像 (QEMU)
#   .\run-qemu.ps1          -> 弹出窗口, 直接看 VGA 文本输出
#   .\run-qemu.ps1 -Verify  -> 无窗口, 捕获串口输出验证启动(6 秒)
# 依赖: qemu-system-x86_64 (winget install SoftwareFreedomConservancy.QEMU)
# 注意: 内核为 64 位长模式, 必须用 qemu-system-x86_64 (qemu-system-i386
#       不支持 64 位 guest, wrmsr 设置 EFER.LME 无效)。
# ============================================================
param(
    [switch]$Verify,
    [switch]$Iso
)

$qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (-not $qemu) {
    # Get-Command 在某些 shell 的 PATH 下找不到, 退而扫描常见安装位置
    $qemuCands = @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\Program Files\qemu\qemu-system-x86_64.exe"
    )
    foreach ($c in $qemuCands) {
        if (Test-Path $c) { $qemu = [PSCustomObject]@{ Source = $c }; break }
    }
}
if (-not $qemu) {
    Write-Host "[!] qemu-system-x86_64 not found." -ForegroundColor Yellow
    Write-Host "    Install: winget install SoftwareFreedomConservancy.QEMU" -ForegroundColor Gray
    exit 1
}

if ($Iso) {
    # 像其他系统一样从 ISO (El Torito) 引导
    # 注意: 局部变量不可命名为 $iso —— PowerShell 变量名大小写不敏感, 会与
    # [switch]$Iso 参数变量冲突, 赋值时触发 "无法将 String 转换为 SwitchParameter"。
    $isoPath = Join-Path (Split-Path -Parent $PSScriptRoot) "iso\FSOS.iso"
    if (-not (Test-Path $isoPath)) {
        Write-Host "[!] iso/FSOS.iso not found. Run .\build-mingw.ps1 first." -ForegroundColor Yellow
        exit 1
    }
    if ($Verify) {
        Write-Host "[QEMU] Verifying ISO boot via serial (COM1), 6s..." -ForegroundColor Cyan
        $p = Start-Process -FilePath $qemu.Source `
            -ArgumentList "-cdrom","$isoPath","-nographic" `
            -NoNewWindow -PassThru `
            -RedirectStandardOutput "$env:TEMP\qemu_serial.log" `
            -RedirectStandardError "$env:TEMP\qemu_serial.err"
        Start-Sleep -Seconds 6
        if (-not $p.HasExited) { $p | Stop-Process -Force }
        Write-Host "---- serial output ----" -ForegroundColor Gray
        Get-Content "$env:TEMP\qemu_serial.log" -ErrorAction SilentlyContinue
    } else {
        Write-Host "[QEMU] Launching ISO (VGA window)..." -ForegroundColor Cyan
        & $qemu.Source -cdrom "$isoPath"
    }
    return
}

$img = Join-Path $PSScriptRoot "output\image.img"
if (-not (Test-Path $img)) {
    Write-Host "[!] output/image.img not found. Run .\build-mingw.ps1 first." -ForegroundColor Yellow
    exit 1
}

if ($Verify) {
    Write-Host "[QEMU] Verifying boot via serial (COM1), 6s..." -ForegroundColor Cyan
    $p = Start-Process -FilePath $qemu.Source `
        -ArgumentList "-drive","file=$img,format=raw,index=0,media=disk","-nographic" `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput "$env:TEMP\qemu_serial.log" `
        -RedirectStandardError "$env:TEMP\qemu_serial.err"
    Start-Sleep -Seconds 6
    if (-not $p.HasExited) { $p | Stop-Process -Force }
    Write-Host "---- serial output ----" -ForegroundColor Gray
    Get-Content "$env:TEMP\qemu_serial.log" -ErrorAction SilentlyContinue
} else {
    Write-Host "[QEMU] Launching (VGA window)..." -ForegroundColor Cyan
    & $qemu.Source -drive file=$img,format=raw,index=0,media=disk
}
