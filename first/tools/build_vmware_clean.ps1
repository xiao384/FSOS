# FSOS clean VMware build helper
# Run from first\ on a Windows machine with MinGW/NASM/Python/qemu-img.
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Here\..\
python tools\fsos.py build --arch x64 --with-python --clean
if ($LASTEXITCODE -ne 0) { throw "FSOS build failed" }
python tools\fsos.py pack --uefi --vmdk
if ($LASTEXITCODE -ne 0) { throw "UEFI/VMware packaging failed" }
Write-Host "VMware files are in output\Auto" -ForegroundColor Green
