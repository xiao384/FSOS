# make_uefi_vm.ps1 - 统一调用跨平台 UEFI/VMware 打包器。
# 不再硬编码宿主机路径，也不允许把过期 kernel/UEFI loader 直接塞进 VMware 镜像。
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Py = Get-Command python -ErrorAction SilentlyContinue
if (-not $Py) { throw 'Python 3.x not found on PATH' }
& $Py.Source (Join-Path $ScriptDir 'tools\fsos.py') pack --uefi --vmdk
if ($LASTEXITCODE -ne 0) { throw "UEFI/VMware packaging failed: exit $LASTEXITCODE" }
Write-Host "Clean UEFI/VMware files are in output\Auto" -ForegroundColor Green
