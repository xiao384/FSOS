# make_uefi_vm.ps1 - 生成 UEFI 启动的 FSOS VMware 文件 (不改动原有 BIOS/HDD 路径)
#   编译 BOOTX64.EFI -> 用 make_uefi_disk.py 打包 GPT+FAT32(ESP), 其中 ESP 含
#     EFI/BOOT/BOOTX64.EFI + 根目录 KERNEL.BIN (内核文件, 由加载器按文件名读取)
#   -> qemu-img 转 vmdk -> 写 FSOS_UEFI.vmx (firmware="efi")
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutRoot   = "E:\project\clion\project_system\output"
$FOut      = Join-Path $ScriptDir "output"
$BuildDir  = Join-Path $FOut "uefi_build"
$qemuImg  = "C:\Program Files\qemu\qemu-img.exe"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# 0. 删除旧的 VM 文件 (避免陈旧 .nvram 让 VMware 沿用错误的 32/64 位 EFI 配置,
#    或旧 vmdk 残留导致黑屏/启动异常). 用户要求: 每次改完系统必须重生成并清掉旧的.
$vmExts = @('.vmdk','.vmx','.nvram','.vmxf','.vmsd','.vmem','.log')
foreach ($ext in $vmExts) {
    $f = Join-Path $OutRoot ("FSOS_UEFI" + $ext)
    if (Test-Path $f) { Remove-Item $f -Force; Write-Host ("  removed old {0}" -f $f) -ForegroundColor Yellow }
}
# vmx 里 serial0.fileName = "vmware-uefi-serial.log" 不在上面的命名模式内:
# 若旧文件存在, VMware 启动会弹 "replace or append" 对话框并卡住引导, 必须一并清掉
$sf = Join-Path $OutRoot "vmware-uefi-serial.log"
if (Test-Path $sf) { Remove-Item $sf -Force; Write-Host ("  removed old {0}" -f $sf) -ForegroundColor Yellow }

# 1. 编译 BOOTX64.EFI (MinGW PE32+ EFI 应用, subsystem 10)
Write-Host "[UEFI] Compiling BOOTX64.EFI ..." -ForegroundColor Cyan

# 内核入口线性地址: 必须与 BIOS/loader.asm 使用的入口完全一致。
# kernel_clean.bin 已按 Multiboot2 magic 裁剪, 故 "文件偏移 X" 对应加载地址 0x100000+X;
# nm 给出的 _start 是绝对线性地址 (multiboot2 头 24B + 对齐 => 0x100020), 可直接当入口。
# ⚠️ 踩坑: 曾写死 0x100000 —— 那是 multiboot2 头(数据, 首字节 0xd6 在 64 位下是非法指令),
#    跳过去立刻 #UD 重启, 表现为 "ExitBootServices OK 后一跳就重启"。
$kernelEntry = [uint64]0x100020
$kernelExe = Join-Path $FOut "kernel.exe"
if (Test-Path $kernelExe) {
    $nmOut = & nm "$kernelExe" 2>$null
    $startLine = ($nmOut | Where-Object { $_ -match '\b_start\b' -and $_ -match '\sT\s' } | Select-Object -First 1)
    if ($startLine) {
        $v = [Convert]::ToUInt64((($startLine.Trim() -split '\s+')[0]), 16)
        if ($v -gt 0x100000) { $kernelEntry = $v }
    }
}
Write-Host ("  -> kernel entry = 0x{0:X} (from _start)" -f $kernelEntry) -ForegroundColor Gray
# 关键: 用 -fno-pic -fno-pie 编译位置相关代码, 所有内部引用在链接期解析为相对 0x400000 的
#       绝对地址。链接【不】用 -shared (也不要 IMAGE_FILE_DLL 标志): 即生成普通 PE 应用,
#       EDK2/VMware 的 PE 加载器对"无重定位表的普通应用"会原样加载到首选基址 0x400000,
#       此时位置相关代码天然正确, 无需 .reloc 段。
#       (踩坑: 之前用 -shared + DLL 标志, MinGW 自体无法为自包含镜像生成 .reloc 段,
#        导致 DLL 型 PE 缺重定位表 -> VMware EFI 报 "No compatible bootloader found" 拒绝。)
& cmd /c "gcc -c -fno-pic -fno-pie -ffreestanding -fno-stack-protector -mgeneral-regs-only -mno-red-zone -m64 -DKERNEL_ENTRY=0x$($kernelEntry.ToString('X'))ULL -I `"$ScriptDir\boot\uefi`" -o `"$BuildDir\uefi_main.o`" `"$ScriptDir\boot\uefi\main.c`" 2>&1"
if ($LASTEXITCODE -ne 0) { throw "compile uefi main.c failed" }
# 链接为 PE32+ EFI 应用 (DLL 型, 带 IMAGE_FILE_DLL 标志, VMware EFI 才接受):
#   -shared            => 生成 DLL 型 PE (Characteristics 含 0x2000), 固件按 EFI 规范加载;
#   --image-base,0x400000 => 首选加载基址, 避开内核 0x100000;
#   --subsystem,10 + -e efi_main => EFI 应用程序入口。
#   注意: MinGW 不会为自包含镜像生成 .reloc, 因此链接后由 fix_uefi_pe.py 手工注入
#         一个最小合法 .reloc 段, 否则 VMware EFI 报 "No compatible bootloader found" 拒绝。
& cmd /c "gcc -nostdlib -shared -Wl,--subsystem,10 -Wl,-e,efi_main -Wl,--image-base,0x400000 -o `"$FOut\BOOTX64.EFI`" `"$BuildDir\uefi_main.o`" 2>&1"
if ($LASTEXITCODE -ne 0) { throw "link BOOTX64.EFI failed" }
# 链接后修正 PE 头: 强制 subsystem=10, 清空空 import 目录 (MinGW 在 -nostdlib 下偶发产生)
& python "$ScriptDir\tools\fix_uefi_pe.py" "$FOut\BOOTX64.EFI" "$FOut\BOOTX64.EFI"
Write-Host ("  -> BOOTX64.EFI {0} bytes" -f (Get-Item "$FOut\BOOTX64.EFI").Length) -ForegroundColor Green

# 内核干净镜像 (由 build-mingw.ps1 -WithPython 导出, 不含安装器 payload)
$kclean = Join-Path $FOut "kernel_clean.bin"
if (-not (Test-Path $kclean)) { throw "kernel_clean.bin not found; run build-mingw.ps1 -WithPython first" }

# 2. 打包 UEFI 磁盘镜像 (KERNEL.BIN 作为 ESP 根目录文件, 加载器按文件名读取)
$diskImg = Join-Path $FOut "uefi_disk.img"
Write-Host "[UEFI] Packing GPT+FAT32 disk (KERNEL.BIN as ESP file) ..." -ForegroundColor Cyan
& python "$ScriptDir\tools\make_uefi_disk.py" --kernel $kclean --efi "$FOut\BOOTX64.EFI" --cint "$FOut\CINT.MOD" --jvm "$FOut\JVM.MOD" --out $diskImg
if ($LASTEXITCODE -ne 0) { throw "make_uefi_disk.py failed" }
# 同时把内核文件单独导出到 output/, 便于日后单独替换/升级 (OS 文件式形态)
Copy-Item -Path $kclean -Destination (Join-Path $FOut "KERNEL.BIN") -Force
Write-Host ("  -> KERNEL.BIN ({0} bytes) exported for file-based boot" -f (Get-Item $kclean).Length) -ForegroundColor Green

# 3. 转 vmdk
$vmdk = Join-Path $OutRoot "FSOS_UEFI.vmdk"
if (-not (Test-Path $qemuImg)) { throw "qemu-img.exe not found at $qemuImg" }
& $qemuImg convert -f raw -O vmdk $diskImg $vmdk
if ($LASTEXITCODE -ne 0) { throw "qemu-img convert failed" }
Write-Host ("  -> {0} ({1} KB)" -f $vmdk, [math]::Round((Get-Item $vmdk).Length/1KB,1)) -ForegroundColor Green

# 4. 写 VMX (UEFI 固件)
$vmx = Join-Path $OutRoot "FSOS_UEFI.vmx"
$vmxText = @'
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "10"
virtualHW.productCompatibility = "hosted"
memsize = "4096"
displayName = "FSOS (UEFI boot)"
guestOS = "other-64"   # 必须是 64 位客户机, 否则 VMware 加载 32-bit EFI, 无法执行 64 位 BOOTX64.EFI (报 "No Media")
firmware = "efi"
numvcpus = "1"
cpuid.coresPerSocket = "1"
floppy0.present = "FALSE"
ide0:0.present = "TRUE"
ide0:0.fileName = "FSOS_UEFI.vmdk"
ide0:0.deviceType = "disk"
ide0:0.mode = "persistent"
ide0:0.redo = ""
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "vmware-uefi-serial.log"
serial0.startConnected = "TRUE"
serial0.yieldOnMsrRead = "TRUE"
svga.autodetect = "TRUE"
keyboard.typematic = "TRUE"
# VNC 远端显示: 无头启动 (vmrun start nogui) 时无法用 vmrun captureScreen (需 VMware Tools),
# 只能通过 VNC 取帧缓冲来截图验证 GUI。见 tools/vnc_screenshot.py。
RemoteDisplay.vnc.enabled = "TRUE"
RemoteDisplay.vnc.port = "5901"
'@
Set-Content -Path $vmx -Value $vmxText -Encoding ASCII
Write-Host ("[OK] created {0} (firmware=efi)" -f $vmx)
Write-Host "Done. Open: $vmx"
