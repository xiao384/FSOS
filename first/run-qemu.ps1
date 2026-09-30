# ============================================================
# 运行内核镜像 (QEMU) - 薄转发壳, 转发到 fsos.py run / verify
#   .\run-qemu.ps1          -> fsos.py run (QEMU VGA 窗口)
#   .\run-qemu.ps1 -Verify  -> fsos.py verify (串口哨兵验证, 25s)
#   .\run-qemu.ps1 -Iso     -> fsos.py run (ISO 模式, 经 cdrom 引导)
#   .\run-qemu.ps1 -Verify -Iso -> fsos.py verify --mode cdrom
# 依赖: qemu-system-x86_64, python 3.12+
# ============================================================
param(
    [switch]$Verify,
    [switch]$Iso
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

if ($Verify) {
    if ($Iso) {
        Write-Host "[run-qemu] Forwarding to fsos.py verify --mode cdrom ..." -ForegroundColor Cyan
        & $py.Source $FsosPy verify --mode cdrom --timeout 25 --no-gdb
    } else {
        Write-Host "[run-qemu] Forwarding to fsos.py verify ..." -ForegroundColor Cyan
        & $py.Source $FsosPy verify --timeout 25 --no-gdb
    }
    exit $LASTEXITCODE
} else {
    if ($Iso) {
        # ISO 模式: 直接调用 QEMU -cdrom (fsos.py run 仅支持 image.img)
        $qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
        if (-not $qemu) {
            $qemuCands = @(
                "C:\Program Files\qemu\qemu-system-x86_64.exe",
                "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
                "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe"
            )
            foreach ($c in $qemuCands) {
                if (Test-Path $c) { $qemu = [PSCustomObject]@{ Source = $c }; break }
            }
        }
        if (-not $qemu) {
            Write-Host "[!] qemu-system-x86_64 not found." -ForegroundColor Yellow
            Write-Host "    Install: winget install SoftwareFreedomConservancy.QEMU" -ForegroundColor Gray
            exit 4
        }
        $isoPath = Join-Path (Split-Path -Parent $ScriptDir) "iso\FSOS.iso"
        if (-not (Test-Path $isoPath)) {
            Write-Host "[!] iso/FSOS.iso not found. Run .\pack_iso.ps1 first." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "[QEMU] Launching ISO (VGA window)..." -ForegroundColor Cyan
        & $qemu.Source -cdrom "$isoPath"
        exit $LASTEXITCODE
    } else {
        Write-Host "[run-qemu] Forwarding to fsos.py run ..." -ForegroundColor Cyan
        & $py.Source $FsosPy run
        exit $LASTEXITCODE
    }
}
