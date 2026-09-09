# set-boot-hdd.ps1
#
# After the graphical install finishes and the VM reboots, switch the VMware
# boot order from "cdrom,hdd" back to "hdd" so it no longer re-enters the installer.
#
# Usage:
#   .\set-boot-hdd.ps1                              # default: output\vmware-iso\FSOS.vmx
#   .\set-boot-hdd.ps1 -Vmx D:\vms\pxs\FSOS.vmx

param(
    [string]$Vmx = (Resolve-Path -ErrorAction SilentlyContinue "E:\project\clion\project_system\output\FSOS.vmx")
)

if (!(Test-Path $Vmx)) {
    Write-Error "VMX not found: $Vmx"
    exit 1
}

$lines = Get-Content $Vmx
$out = @()
$changed = $false
foreach ($l in $lines) {
    if ($l -match '^\s*bios\.bootOrder\s*=') {
        $out += 'bios.bootOrder = "hdd"'
        $changed = $true
    } else {
        $out += $l
    }
}
if (!$changed) { $out += 'bios.bootOrder = "hdd"' }

Set-Content -Path $Vmx -Value $out -Encoding ASCII
Write-Host "Boot order set to hdd: $Vmx"
Write-Host "You can now open the VM and it will boot from the hard disk."
