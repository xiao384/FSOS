$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir
Set-Location $Root

Write-Host '[FSOS v9] Verifying GUI/font source contracts...' -ForegroundColor Cyan
python -m unittest discover -s tools/tests -p 'test*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'GUI/font regression tests failed.' }

if (-not (Get-Command python -ErrorAction SilentlyContinue)) { throw 'Python is required.' }
if (-not (Get-Command nasm -ErrorAction SilentlyContinue)) { throw 'NASM is required; install NASM and retry.' }
if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) { throw 'GCC/MinGW is required; install it and retry.' }
if (-not (Get-Command qemu-img -ErrorAction SilentlyContinue)) { throw 'qemu-img is required for VMware VMDK packaging; install QEMU and add qemu-img.exe to PATH.' }

Write-Host '[FSOS v9] Full clean build...' -ForegroundColor Yellow
python tools/fsos.py clean --arch x64
if ($LASTEXITCODE -ne 0) { throw 'Clean failed.' }
python tools/fsos.py build --arch x64 --with-python --clean
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

Write-Host '[FSOS v9] Packaging UEFI + VMware...' -ForegroundColor Yellow
python tools/fsos.py pack --uefi --vmdk
if ($LASTEXITCODE -ne 0) { throw 'UEFI/VMware packaging failed.' }

Write-Host '[FSOS v9] Done. Check output/Auto for the fresh VMware files.' -ForegroundColor Green
