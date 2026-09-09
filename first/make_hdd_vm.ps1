# make_hdd_vm.ps1 - 用之前验证成功的 HDD 启动路径生成 VMware 文件
#   把 image.img(裸盘镜像, 含 boot.bin+loader+内核) 转成 VMware vmdk,
#   并写一个从硬盘启动的 VMX。绕过一直 triple fault 的 El Torito ISO 加载器。
$ErrorActionPreference = 'Stop'
$out = "E:\project\clion\project_system\output"
$img = "E:\project\clion\project_system\first\output\image.img"
$qemuImg = "C:\Program Files\qemu\qemu-img.exe"
$vmdk = Join-Path $out "FSOS_HDD.vmdk"
$vmx  = Join-Path $out "FSOS_HDD.vmx"

# 关掉可能锁住 output 的 VMware 进程
Get-Process -Name vmplayer, vmware, vmware-vmx -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# 清理旧 ISO 启动残留与崩溃产物, 只留 HDD 启动文件
Get-ChildItem $out -File | Where-Object {
    $_.Name -like "FSOS.*" -or $_.Name -like "vmware*" -or $_.Name -like "vmmcores*" `
    -or $_.Name -eq "nvram" -or $_.Name -like "*.scoreboard" -or $_.Name -like "*.vmem" `
    -or $_.Name -like "*.lck" -or $_.Name -like "*.dmp"
} | Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem $out -Directory -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

# 1. 转换裸盘镜像为 VMware vmdk
& $qemuImg convert -f raw -O vmdk $img $vmdk
if ($LASTEXITCODE -ne 0) { throw "qemu-img convert failed" }
Write-Host ("[OK] created {0} ({1} KB)" -f $vmdk, [math]::Round((Get-Item $vmdk).Length/1KB,1))

# 2. 写从硬盘启动的 VMX
$vmxText = @'
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "10"
virtualHW.productCompatibility = "hosted"
memsize = "4096"
displayName = "FSOS (HDD boot)"
guestOS = "other"
bios.bootOrder = "hdd"
numvcpus = "1"
cpuid.coresPerSocket = "1"

# disable floppy so SeaBIOS does not try floppy emulation
floppy0.present = "FALSE"

# IDE disk: image.img 转换得到的可启动盘 (boot.bin + loader + kernel)
ide0:0.present = "TRUE"
ide0:0.fileName = "FSOS_HDD.vmdk"
ide0:0.deviceType = "disk"
ide0:0.mode = "persistent"
ide0:0.redo = ""

# COM1 serial logging: kernel prints boot messages to 0x3F8 (9600 8N1)
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "vmware-hdd-serial.log"
serial0.startConnected = "TRUE"
serial0.yieldOnMsrRead = "TRUE"

svga.autodetect = "TRUE"
keyboard.typematic = "TRUE"
'@
Set-Content -Path $vmx -Value $vmxText -Encoding ASCII
Write-Host ("[OK] created {0} (bootOrder=hdd)" -f $vmx)
Write-Host "Done. Open: $vmx"
