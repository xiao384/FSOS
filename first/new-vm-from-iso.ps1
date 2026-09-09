# new-vm-from-iso.ps1
#
# Create a "boot-from-CD-and-install" VMware virtual machine from the existing
# FSOS.iso, similar to installing Windows from a setup disc:
#   create VM -> blank hard disk -> attach CD/DVD to the ISO -> boot from CD ->
#   graphical installer writes the OS to the disk -> reboot from hard disk.
#
# After install completes, run  .\set-boot-hdd.ps1  to switch the boot order
# back to the hard disk (or use VMware's firmware boot menu).
#
# Usage:
#   .\new-vm-from-iso.ps1                 # uses ../iso/FSOS.iso, output/vmware-iso/
#   .\new-vm-from-iso.ps1 -Iso .\..\iso\FSOS.iso -OutDir D:\vms\pxs
#   .\new-vm-from-iso.ps1 -NoStart        # only create files, do not launch VMware

param(
    [string]$Iso      = (Resolve-Path -ErrorAction SilentlyContinue "$PSScriptRoot\..\iso\FSOS.iso"),
    [string]$OutDir   = "E:\project\clion\project_system\output",
    [int]   $MemMB    = 4096,
    [switch]$NoStart
)

$ErrorActionPreference = 'Stop'

# ---- 0. validate ISO ----
if (!(Test-Path $Iso)) {
    Write-Error "ISO not found: $Iso`nRun .\pack_iso.ps1 to build iso\FSOS.iso first."
    exit 1
}
Write-Host "[1/5] ISO: $Iso ($([math]::Round((Get-Item $Iso).Length/1MB,2)) MB)"

# ---- 1. locate VMware ----
$vmwareCandidates = @(
    'C:\Program Files (x86)\VMware\VMware Workstation\vmware.exe',
    'C:\Program Files\VMware\VMware Workstation\vmware.exe',
    'C:\Program Files (x86)\VMware\VMware Player\vmplayer.exe',
    'C:\Program Files\VMware\VMware Player\vmplayer.exe'
)
$vmware = $null
foreach ($c in $vmwareCandidates) { if (Test-Path $c) { $vmware = $c; break } }
if (!$vmware) {
    $p = Get-Command vmware.exe, vmplayer.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p) { $vmware = $p.Source }
}
if ($vmware) { Write-Host "[2/5] VMware: $vmware" } else {
    Write-Warning "VMware not found on this machine. Will only generate the VMX for manual opening."
}

# ---- 2. prepare dir + blank disk ----
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$vmxPath  = Join-Path $OutDir 'FSOS.vmx'
$diskPath = Join-Path $OutDir 'FSOS.vmdk'

$vdiskMgr = if ($vmware) { Join-Path (Split-Path $vmware) 'vmware-vdiskmanager.exe' } else { $null }
if ($vmware -and (Test-Path $vdiskMgr)) {
    & $vdiskMgr -c -s 2MB -a ide -t 1 $diskPath | Out-Null
    Write-Host "[3/5] Created blank disk: $diskPath"
} else {
    # hand-written minimal VMDK (monolithicFlat, 4096 sectors = 2MB) + zero flat
    $header = @'
# Disk DescriptorFile
version=1
CID=fffffffe
parentCID=ffffffff
createType="monolithicFlat"

# Extent description
RW 4096 FLAT "FSOS-flat.vmdk" 0

# The disk Data Base
ddb.virtualHWVersion = "20"
ddb.geometry.cylinders = "4"
ddb.geometry.heads = "16"
ddb.geometry.sectors = "63"
ddb.adapterType = "ide"
'@
    Set-Content -Path $diskPath -Value $header -Encoding ASCII
    $flat = Join-Path $OutDir 'FSOS-flat.vmdk'
    $zero = New-Object byte[] (2 * 1024 * 1024)
    [System.IO.File]::WriteAllBytes($flat, $zero)
    Write-Host "[3/5] Generated placeholder blank disk: $diskPath (+ 2MB flat)"
}

# ---- 4. write VMX (CD boot, blank disk attached) ----
$isoEsc = $Iso -replace '\\', '\\'
$vmx = @"
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "10"
virtualHW.productCompatibility = "hosted"
memsize = "$MemMB"
displayName = "FSOS (ISO install)"
guestOS = "other"
bios.bootOrder = "cdrom,hdd"
numvcpus = "1"
cpuid.coresPerSocket = "1"

# disable floppy so SeaBIOS does not try floppy emulation
floppy0.present = "FALSE"

# blank IDE disk (install target)
ide0:0.present = "TRUE"
ide0:0.fileName = "FSOS.vmdk"
ide0:0.deviceType = "disk"
ide0:0.mode = "persistent"
ide0:0.redo = ""

# CD/DVD pointing at our ISO (install source)
ide1:0.present = "TRUE"
ide1:0.deviceType = "cdrom-image"
ide1:0.fileName = "$isoEsc"
ide1:0.startConnected = "TRUE"

# COM1 serial logging: kernel prints boot messages to 0x3F8 (9600 8N1)
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "vmware-serial.log"
serial0.startConnected = "TRUE"
serial0.yieldOnMsrRead = "TRUE"

svga.autodetect = "TRUE"
keyboard.typematic = "TRUE"
"@
Set-Content -Path $vmxPath -Value $vmx -Encoding ASCII
Write-Host "[4/5] Created VMX: $vmxPath (bootOrder=cdrom,hdd)"

# ---- 5. launch ----
Write-Host "[5/5] Done."
if ($vmware -and !$NoStart) {
    Write-Host "Launching VMware and booting from CD..."
    Start-Process -FilePath $vmware -ArgumentList "`"$vmxPath`"" -WindowStyle Normal
    Write-Host "After install completes and reboots, run .\set-boot-hdd.ps1 to switch boot to the hard disk."
} else {
    Write-Host "Open manually: $vmxPath"
    Write-Host "After install, run .\set-boot-hdd.ps1 to set boot order back to the hard disk."
}
