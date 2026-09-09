# run-vmware.ps1 - Run or install FSOS under VMware
#
# Usage:
#   1) Run with the prebuilt hard-disk image:
#        .\run-vmware.ps1
#      (needs output\vmware\image.vmdk + FSOS.vmx, produced by build-mingw.ps1)
#
#   2) Graphical install from ISO (like a Windows setup disc):
#        .\run-vmware.ps1 -Iso ..\iso\FSOS.iso
#      creates a new VM (output\vmware-iso\) with an empty disk + the ISO attached,
#      boots into the graphical installer, then after install run
#      .\set-boot-hdd.ps1 to switch the boot order back to the hard disk.

$ErrorActionPreference = 'Stop'

# parse -Iso <path> from $args
$Iso = ""
for ($i = 0; $i -lt $args.Count; $i++) {
    if ($args[$i] -eq '-Iso' -and ($i + 1) -lt $args.Count) {
        $Iso = $args[$i + 1]; $i++
    }
}

function Find-VMware {
    $cands = @(
        "C:\Program Files (x86)\VMware\VMware Workstation\vmware.exe",
        "C:\Program Files\VMware\VMware Workstation\vmware.exe",
        "C:\Program Files (x86)\VMware\VMware Player\vmplayer.exe",
        "C:\Program Files\VMware\VMware Player\vmplayer.exe"
    )
    $found = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $found) {
        $p = Get-Command vmware.exe, vmplayer.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($p) { $found = $p.Source }
    }
    return $found
}

if ($Iso) {
    if (-not (Test-Path $Iso)) {
        Write-Host "ISO not found: $Iso" -ForegroundColor Red
        Write-Host "Run .\pack_iso.ps1 first to build iso\FSOS.iso" -ForegroundColor Yellow
        exit 1
    }
    $vm = Find-VMware
    if (-not $vm) {
        Write-Host "VMware not detected. Generated the ISO-install VMX under:" -ForegroundColor Yellow
        Write-Host "  output\vmware-iso\" -ForegroundColor Yellow
        Write-Host "Install VMware Workstation/Player, then open FSOS.vmx manually." -ForegroundColor Yellow
        & "$PSScriptRoot\new-vm-from-iso.ps1" -Iso $Iso -NoStart
        exit 0
    }
    Write-Host "Launching VMware and booting from ISO..." -ForegroundColor Cyan
    & "$PSScriptRoot\new-vm-from-iso.ps1" -Iso $Iso
    exit 0
}

# ---- run with the prebuilt hard-disk image ----
$vmDir  = Join-Path $PSScriptRoot "output\vmware"
$vmx    = Join-Path $vmDir "FSOS.vmx"
$vmdk   = Join-Path $vmDir "image.vmdk"

$hasVmx  = Test-Path $vmx
$hasVmdk = Test-Path $vmdk
if (-not $hasVmx -or -not $hasVmdk) {
    Write-Host "Missing VM files. Options:" -ForegroundColor Red
    Write-Host "  a) Run build-mingw.ps1 -WithPython to rebuild image.vmdk" -ForegroundColor Yellow
    Write-Host "  b) Install from ISO: .\run-vmware.ps1 -Iso ..\iso\FSOS.iso" -ForegroundColor Yellow
    exit 1
}

$vm = Find-VMware
if (-not $vm) {
    Write-Host "VMware Workstation / Player not found." -ForegroundColor Yellow
    Write-Host "1) Download free VMware Player from vmware.com" -ForegroundColor Yellow
    Write-Host "2) Open this file (double-click or File -> Open):" -ForegroundColor Yellow
    Write-Host "   $vmx" -ForegroundColor Yellow
    exit 0
}

Write-Host "Launching $vm" -ForegroundColor Cyan
Write-Host "VM: $vmx" -ForegroundColor Cyan
& $vm $vmx
