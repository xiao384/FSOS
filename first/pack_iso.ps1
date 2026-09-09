# ============================================================
# pack_iso.ps1 - 生成可启动 ISO (El Torito No-Emulation, 纯 PowerShell)
#
# 无外部依赖: 不需要 grub-mkrescue / xorriso / 交叉工具链。
# 直接手写 ISO 9660 卷描述 + El Torito Boot Catalog, 自带一个 No-Emulation
# 引导加载器 iso_boot.bin, 它经 int13h AH=0x42 从 CD 直接读取 kernel.bin 到
# 0x10000, 再拷到 0x100000, 跳内核入口。这是 ISOLINUX/GRUB 等"其他系统"
# 普遍采用的 El Torito 引导方式。
#
# BIOS 引导流程 (no-emulation):
#   1. BIOS 读 Boot Catalog -> media type 0x00 (no emulation)
#   2. BIOS 把 iso_boot.bin (1 个 2048 字节扇区) 加载到 0x0000:0x7C00
#   3. iso_boot.bin 运行: 开 A20 -> 从 CD 扩展读 kernel.bin -> 拷字体到 0xB0000
#      -> 进保护模式 -> 拷内核 0x10000->0x100000 -> jmp KERNEL_ENTRY
#   4. 内核经 ATA PIO 驱动读写持久化区 (LBA 3800/3810/3960)
#
# 注: 持久化区在真实硬盘上; 纯 CD 启动(无硬盘)时内核仍可引导并进入 GUI,
#     只是用户/配置不会被保存 (与常见的 live CD 行为一致)。要获得完整持久化,
#     像其他系统一样: 从本 ISO 启动, 并把 image.img 写到一块硬盘(-drive)。
#
# ISO 布局 (逻辑扇区 2048 字节):
#   0x000-0x00F  系统区(16 扇区)
#   0x010        PVD (Primary Volume Descriptor)
#   0x011        El Torito Boot Record
#   0x012        Volume Descriptor Terminator
#   0x013-0x014  Boot Catalog (2 扇区)
#   0x015        Path Table (Type L)
#   0x016        Path Table (Type M)
#   0x017        Root Directory
#   0x018        iso_boot.bin (引导加载器, 1 扇区)
#   0x019        KERNEL.BIN
#   0x019+N      BOOT.BIN
#   0x019+N+M    IMAGE.IMG
#
# 用法: .\pack_iso.ps1 [-TargetDir <dir>]   (默认 ../iso)
# ============================================================
param(
    [string]$TargetDir = ""
)
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutputDir = Join-Path $ScriptDir "output"
$BuildDir  = Join-Path $ScriptDir "build-mingw"
if ([string]::IsNullOrEmpty($TargetDir)) {
    $TargetDir = Join-Path (Split-Path -Parent $ScriptDir) "iso"
}
New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
New-Item -ItemType Directory -Force -Path $BuildDir  | Out-Null

# ---------- 1. 准备输入文件 ----------
$imgFile   = Join-Path $OutputDir "image.img"
$kernelB   = Join-Path $OutputDir "kernel.bin"
$bootB     = Join-Path $OutputDir "boot.bin"
$kernelE   = Join-Path $OutputDir "kernel.exe"
if (-not (Test-Path $imgFile))  { throw "missing $imgFile - run .\build-mingw.ps1 first" }
if (-not (Test-Path $kernelB))  { throw "missing $kernelB" }
if (-not (Test-Path $bootB))    { throw "missing $bootB" }
if (-not (Test-Path $kernelE))  { throw "missing $kernelE" }

$imgBytes    = [System.IO.File]::ReadAllBytes($imgFile)
$kernelBytes = [System.IO.File]::ReadAllBytes($kernelB)
$bootBytes   = [System.IO.File]::ReadAllBytes($bootB)
if ($imgBytes.Length % 512 -ne 0)    { throw "image.img size is not a multiple of 512" }
if ($kernelBytes.Length % 2048 -eq 0) { }  # kernel.bin 允许任意长度

# ---------- 2. 解析 kernel 入口地址 ----------
$nmOut = & nm "$kernelE" 2>$null
$startLine = ($nmOut | Where-Object { $_ -match ' T _start$' }) | Select-Object -First 1
if (-not $startLine) { throw "Cannot locate _start symbol in kernel.exe" }
$kernelEntry = [Convert]::ToUInt32(($startLine -split '\s+')[0], 16)
Write-Host ("[NM] kernel entry = 0x{0:X8}" -f $kernelEntry) -ForegroundColor Cyan

# ---------- 3. 汇编 ISO 引导加载器 (小体积; 内核从 CD 经 int13h 读取) ----------
$isoBootObj = Join-Path $BuildDir "iso_boot.obj"
$isoBootBin = Join-Path $BuildDir "iso_boot.bin"

# 先以占位 LBA 汇编一遍取得镜像体积, 据此算出内核在 ISO 中的 LBA, 再重汇编注入。
# (iso_boot 体积与 KERNEL_LBA/KERNEL_SECT 常量取值无关, 重汇编体积不变)
& nasm -f bin `
    -d KERNEL_ENTRY=0x$($kernelEntry.ToString('X')) `
    -d "KERNEL_LBA=24" `
    -d "KERNEL_SECT=1" `
    -i $ScriptDir `
    -i "$ScriptDir\boot" `
    -i $BuildDir `
    -o $isoBootBin `
    "$ScriptDir\boot\iso_boot.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (iso_boot.asm pass1) failed" }
$isoBootBytes = [System.IO.File]::ReadAllBytes($isoBootBin)

$bootLoadSectors = [math]::Ceiling($isoBootBytes.Length / 2048)
$kernelLBA       = 24 + $bootLoadSectors          # 内核文件起始 LBA (2048 字节扇区)
$kernelSectors   = [int]([math]::Ceiling($kernelBytes.Length / 2048))

# 二次汇编: 注入真实 LBA / 扇区数
& nasm -f bin `
    -d KERNEL_ENTRY=0x$($kernelEntry.ToString('X')) `
    -d "KERNEL_LBA=$kernelLBA" `
    -d "KERNEL_SECT=$kernelSectors" `
    -i $ScriptDir `
    -i "$ScriptDir\boot" `
    -i $BuildDir `
    -o $isoBootBin `
    "$ScriptDir\boot\iso_boot.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (iso_boot.asm pass2) failed" }
$isoBootBytes = [System.IO.File]::ReadAllBytes($isoBootBin)

# 关键: El Torito 的 "扇区数" 以 512 字节计, 但 ISO 物理扇区是 2048 字节。
# 若引导镜像大小不是 2048 的整数倍, BIOS 按 512 字节加载时会跨越 ISO 扇区边界,
# 某些 BIOS/VMware 会把后半段读成内核数据, 导致加载器被破坏后执行无效内存。
# 因此必须把最终 iso_boot.bin 补齐到 2048 字节整数倍, 并让 Catalog 扇区数 = 该倍数 * 4。
$padLen = [math]::Ceiling($isoBootBytes.Length / 2048) * 2048
if ($padLen -gt $isoBootBytes.Length) {
    $padded = New-Object byte[] $padLen
    [Array]::Copy($isoBootBytes, 0, $padded, 0, $isoBootBytes.Length)
    $isoBootBytes = $padded
}
# 写回补齐后的文件, 供后续 ISO 写入与自检使用
[System.IO.File]::WriteAllBytes($isoBootBin, $isoBootBytes)
Write-Host ("[NASM] iso_boot.bin assembled ({0} bytes, loader only)" -f $isoBootBytes.Length) -ForegroundColor Cyan
Write-Host ("[ISO]  kernel.bin @ LBA {0} ({1} sectors, loaded via int13h)" -f $kernelLBA, $kernelSectors) -ForegroundColor Cyan

# ---------- 4. 布局计算 ----------
$SECTOR = 2048
$sysArea     = 16          # 0x000-0x00F
$pvdSec      = 16          # 0x010
$bootRecSec  = 17          # 0x011
$termSec     = 18          # 0x012
$catSec      = 19          # 0x013 (Boot Catalog, 占 2 扇区)
$ptLSec      = 21          # 0x015
$ptMSec      = 22          # 0x016
$rootDirSec  = 23          # 0x017
$bootLoadSec = 24          # 0x018  iso_boot.bin (含内嵌内核)

# 引导镜像(本加载器, 小体积)占连续扇区, 其后是 kernel.bin(从 CD 读入),
# 再之后是 boot.bin / image.img。
# 显式强转 [int]: [math]::Ceiling 返回 double, 直接用于 [Array]::Copy 会触发 PowerShell
# 在 int/long 重载间的歧义, 导致 "Source array was not long enough" 异常。统一用 [int]。
$bootLoadSectors = [int]([math]::Ceiling($isoBootBytes.Length / $SECTOR))
$bootSectors      = [int]([math]::Ceiling($bootBytes.Length / $SECTOR))    # 1
$imgSectors       = [int]([math]::Ceiling($imgBytes.Length / $SECTOR))     # 1024 (2MB)

$kernelStartSec = [int]($bootLoadSec + $bootLoadSectors)
$bootStartSec   = [int]($kernelStartSec + $kernelSectors)
$imgStartSec    = [int]($bootStartSec + $bootSectors)
$totalSectors   = [int]($imgStartSec + $imgSectors)

Write-Host ("[ISO] boot(loader) @ {0} ({1} sectors), kernel @ {2} ({3}), boot.bin @ {4}, img @ {5}" -f `
    $bootLoadSec, $bootLoadSectors, $kernelStartSec, $kernelSectors, $bootStartSec, $imgStartSec) -ForegroundColor Cyan

# ---------- 5. 字节辅助函数 ----------
function Set-U16LE($b, $o, $v) { $b[$o] = $v -band 0xFF; $b[$o+1] = ($v -shr 8) -band 0xFF }
function Set-U16BE($b, $o, $v) { $b[$o] = ($v -shr 8) -band 0xFF; $b[$o+1] = $v -band 0xFF }
function Set-U32LE($b, $o, $v) { for ($i=0; $i -lt 4; $i++) { $b[$o+$i] = ($v -shr (8*$i)) -band 0xFF } }
function Set-U32BE($b, $o, $v) { for ($i=0; $i -lt 4; $i++) { $b[$o+$i] = ($v -shr (8*(3-$i))) -band 0xFF } }
function Set-BE32($b, $o, $v) { Set-U32LE $b $o $v; Set-U32BE $b ($o+4) $v }
function Set-BE16($b, $o, $v) { Set-U16LE $b $o $v; Set-U16BE $b ($o+2) $v }
function Get-Ascii($s) { [System.Text.Encoding]::ASCII.GetBytes($s) }

function New-DirRecordBytes($extent, $dataLen, $flags, $idBytes) {
    $L = $idBytes.Length
    $base = 33 + $L
    $pad = if ($base % 2 -eq 1) { 1 } else { 0 }
    $recLen = $base + $pad
    $rec = New-Object byte[] $recLen
    $rec[0] = $recLen
    Set-U32LE $rec 2 $extent; Set-U32BE $rec 6 $extent
    Set-U32LE $rec 10 $dataLen; Set-U32BE $rec 14 $dataLen
    $rec[18] = 126; $rec[19] = 1; $rec[20] = 1; $rec[21] = 0; $rec[22] = 0; $rec[23] = 0; $rec[24] = 0
    $rec[25] = $flags
    Set-U32LE $rec 28 1
    $rec[32] = $L
    [Array]::Copy($idBytes, 0, $rec, 33, $L)
    return $rec
}

# ---------- 6. 构造各扇区 ----------
$iso = New-Object byte[] ([int]($totalSectors * $SECTOR))

# --- PVD (0x10) ---
$pvd = New-Object byte[] $SECTOR
$pvd[0] = 0x01
[Array]::Copy((Get-Ascii "CD001"), 0, $pvd, 1, 5)
$pvd[6] = 0x01
[Array]::Copy((Get-Ascii "FSOS"), 0, $pvd, 8, 12)
[Array]::Copy((Get-Ascii "FSOS_ISO"), 0, $pvd, 40, 16)
Set-BE32 $pvd 80 $totalSectors
Set-BE16 $pvd 120 1
Set-BE16 $pvd 124 1
Set-BE16 $pvd 128 2048
Set-BE32 $pvd 132 10
Set-U32LE $pvd 140 $ptLSec
Set-U32BE $pvd 148 $ptMSec
$rootRec = New-Object byte[] 34
$rootRec[0] = 34
Set-U32LE $rootRec 2 $rootDirSec; Set-U32BE $rootRec 6 $rootDirSec
Set-U32LE $rootRec 10 $SECTOR; Set-U32BE $rootRec 14 $SECTOR
$rootRec[18] = 126; $rootRec[19] = 1; $rootRec[20] = 1
$rootRec[25] = 0x02
Set-U32LE $rootRec 28 1
$rootRec[32] = 1; $rootRec[33] = 0
[Array]::Copy($rootRec, 0, $pvd, 156, 34)
$pvd[881] = 0x01
[Array]::Copy($pvd, 0, $iso, $pvdSec * $SECTOR, $SECTOR)

# --- El Torito Boot Record (0x11) ---
# ISO9660 卷描述布局 (0-indexed):
#   0      = 0x00 (Boot Record Indicator)
#   1..5   = "CD001"
#   6      = 0x01 (version)
#   7      = 0x00 (reserved)
#   8..39  = Boot System Identifier ("EL TORITO SPECIFICATION", 32 字节) —— 标准偏移是 8
#   40..71 = Boot Identifier (全 0)
#   72..75 = Boot System Use: Boot Catalog 所在 LBA (32-bit LE)
# 注意: 不同 BIOS 对 LBA 偏移解读不一 (SeaBIOS 读 0x47=71, 严格 ISO9660 读 72).
# 为兼容 VMware 与 SeaBIOS, 把 Boot Catalog LBA 同时写到偏移 71 和 72; 二者重叠写入后,
# 无论 BIOS 读哪个偏移, 得到的都是正确的 catSec.
$br = New-Object byte[] $SECTOR
$br[0] = 0x00
[Array]::Copy((Get-Ascii "CD001"), 0, $br, 1, 5)
$br[6] = 0x01
$etStr = Get-Ascii "EL TORITO SPECIFICATION"
[Array]::Copy($etStr, 0, $br, 7, $etStr.Length)   # 偏移 7 (VMware / SeaBIOS 均按此读取)
# VMware 的 Phoenix BIOS 从 Boot Record 偏移 71 (0x47) 读取 Boot Catalog LBA;
# SeaBIOS 同样读 71. 偏移 72 反而会让 VMware 报告 "No operating system was found".
Set-U32LE $br 71 $catSec
[Array]::Copy($br, 0, $iso, $bootRecSec * $SECTOR, $SECTOR)

# --- Terminator (0x12) ---
$iso[$termSec * $SECTOR] = 0xFF
[Array]::Copy((Get-Ascii "CD001"), 0, $iso, $termSec * $SECTOR + 1, 5)
$iso[$termSec * $SECTOR + 6] = 0x01

# --- Boot Catalog (0x13, 2 扇区) ---
$cat = New-Object byte[] (2 * $SECTOR)
# Validation Entry (offset 0)
$cat[0] = 0x01                              # header id
$cat[1] = 0x00                              # platform = x86
$idStr = Get-Ascii "FSOS Boot"
[Array]::Copy($idStr, 0, $cat, 4, $idStr.Length)
# 关键顺序: 校验和覆盖前 32 字节的 16 个字, 其中偏移 30/31 的 0x55AA 签名
# 也属于被校验范围, 因此必须先写好签名、再计算并回写校验和, 使 16 位累加
# 和末值为 0。旧实现先算和后写签名 -> 实际累加和 = 0xAA55 -> BIOS 判定无效。
$cat[30] = 0x55; $cat[31] = 0xAA
$sum = 0
for ($i = 0; $i -lt 32; $i += 2) { $sum += ([int]$cat[$i]) + 256 * ([int]$cat[$i+1]) }
$ck = (0x10000 - ($sum % 0x10000)) % 0x10000
Set-U16LE $cat 28 $ck
# Default Entry (offset 32): No-Emulation
$cat[32] = 0x88                             # bootable
$cat[33] = 0x00                             # media type: no emulation
Set-U16LE $cat 34 0x07C0                    # load segment: 0x07C0 -> 加载到 0x7C00 (匹配 org 0x7C00)
Set-U16LE $cat 38 ([math]::Ceiling($isoBootBytes.Length / 512))  # 以 512 字节扇区计, 覆盖整段(含内嵌内核)
Set-U32LE $cat 40 $bootLoadSec              # load RBA = iso_boot.bin
[Array]::Copy($cat, 0, $iso, $catSec * $SECTOR, $cat.Length)

# --- Path Table ---
foreach ($ptSec in @($ptLSec, $ptMSec)) {
    $pt = New-Object byte[] $SECTOR
    $pt[0] = 1
    $pt[1] = 0
    if ($ptSec -eq $ptLSec) { Set-U32LE $pt 2 $rootDirSec } else { Set-U32BE $pt 2 $rootDirSec }
    if ($ptSec -eq $ptLSec) { Set-U16LE $pt 6 1 } else { Set-U16BE $pt 6 1 }
    $pt[8] = 0x00
    $pt[9] = 0x00
    [Array]::Copy($pt, 0, $iso, $ptSec * $SECTOR, $SECTOR)
}

# --- Root Directory ---
$rd = New-Object byte[] $SECTOR
$off = 0
function Add-Record([byte[]]$dst, [ref]$off, $extent, $len, $flags, $name) {
    $rec = New-DirRecordBytes $extent $len $flags (Get-Ascii $name)
    [Array]::Copy($rec, 0, $dst, $off.Value, $rec.Length)
    $off.Value += $rec.Length
}
Add-Record $rd ([ref]$off) $rootDirSec $SECTOR 0x02 "."
Add-Record $rd ([ref]$off) $rootDirSec $SECTOR 0x02 ".."
Add-Record $rd ([ref]$off) $kernelStartSec $kernelBytes.Length 0x00 "KERNEL.BIN;1"
Add-Record $rd ([ref]$off) $bootStartSec   $bootBytes.Length   0x00 "BOOT.BIN;1"
Add-Record $rd ([ref]$off) $imgStartSec    $imgBytes.Length    0x00 "IMAGE.IMG;1"
[Array]::Copy($rd, 0, $iso, $rootDirSec * $SECTOR, $SECTOR)

# --- 文件数据 ---
[Array]::Copy($isoBootBytes, 0, $iso, [int]($bootLoadSec    * $SECTOR), $isoBootBytes.Length)
[Array]::Copy($kernelBytes,  0, $iso, [int]($kernelStartSec * $SECTOR), $kernelBytes.Length)
[Array]::Copy($bootBytes,    0, $iso, [int]($bootStartSec   * $SECTOR), $bootBytes.Length)
[Array]::Copy($imgBytes,     0, $iso, [int]($imgStartSec    * $SECTOR), $imgBytes.Length)

# ---------- 7. 写入 ISO ----------
$isoFile = Join-Path $TargetDir "FSOS.iso"
[System.IO.File]::WriteAllBytes($isoFile, $iso)

# ---------- 8. 自检 ----------
Write-Host "[CHECK] Verifying ISO structure ..." -ForegroundColor Cyan
$ok = $true
$bs = [System.IO.File]::ReadAllBytes($isoFile)
$m = Get-Ascii "CD001"
$checks = @(
    "PVD magic      ", $pvdSec,  1,
    "BootRec magic  ", $bootRecSec, 0,
    "Term magic     ", $termSec, 0xFF
)
for ($i = 0; $i -lt $checks.Length; $i += 3) {
    $sec = $checks[$i+1]; $base = $sec * $SECTOR
    $match = ($bs[$base] -eq $checks[$i+2]) -and
             ($bs[$base+1] -eq $m[0]) -and ($bs[$base+2] -eq $m[1]) -and
             ($bs[$base+3] -eq $m[2]) -and ($bs[$base+4] -eq $m[3]) -and ($bs[$base+5] -eq $m[4])
    Write-Host ("  {0} {1}" -f $checks[$i], $(if ($match) {"OK"} else {"FAIL"}))
    if (-not $match) { $ok = $false }
}
$bcBase = $catSec * $SECTOR
$bcOk = ($bs[$bcBase+30] -eq 0x55) -and ($bs[$bcBase+31] -eq 0xAA) -and
        ($bs[$bcBase+32] -eq 0x88) -and ($bs[$bcBase+33] -eq 0x00)
Write-Host ("  BootCatalog(NoEmul)   {0}" -f $(if ($bcOk) {"OK"} else {"FAIL"}))
if (-not $bcOk) { $ok = $false }

# 校验和自检: 前 32 字节的 16 位累加和必须为 0, 否则 BIOS 拒绝引导
$cSum = 0
for ($i = 0; $i -lt 32; $i += 2) { $cSum += ([int]$bs[$bcBase+$i]) + 256 * ([int]$bs[$bcBase+$i+1]) }
$cSum = $cSum % 0x10000
Write-Host ("  CatalogChecksum       {0} (sum16=0x{1:X4})" -f $(if ($cSum -eq 0) {"OK"} else {"FAIL"}), $cSum)
if ($cSum -ne 0) { $ok = $false }

# 引导记录中的目录 LBA 自检 (VMware / SeaBIOS 均读偏移 71)
$brBase = $bootRecSec * $SECTOR
$brCat  = [int]$bs[$brBase+71] + 256 * ([int]$bs[$brBase+72]) +
          65536 * ([int]$bs[$brBase+73]) + 16777216 * ([int]$bs[$brBase+74])
Write-Host ("  BootRecord(CatLBA71={0}) {1}" -f $brCat, $(if ($brCat -eq $catSec) {"OK"} else {"FAIL"}))
if ($brCat -ne $catSec) { $ok = $false }

$isoBootOk = $true
for ($i = 0; $i -lt $isoBootBytes.Length; $i++) {
    if ($bs[$bootLoadSec * $SECTOR + $i] -ne $isoBootBytes[$i]) { $isoBootOk = $false; break }
}
Write-Host ("  BootLoader    {0}" -f $(if ($isoBootOk) {"OK"} else {"FAIL"}))
if (-not $isoBootOk) { $ok = $false }

# 校验内核文件: ISO 中 $kernelStartSec 处应与 kernel.bin 逐字节一致
$kernOk = $true
$kbase = $kernelStartSec * $SECTOR
if ($kbase + $kernelBytes.Length -gt $bs.Length) { $kernOk = $false }
else {
    for ($i = 0; $i -lt $kernelBytes.Length; $i++) {
        if ($bs[$kbase + $i] -ne $kernelBytes[$i]) { $kernOk = $false; break }
    }
}
Write-Host ("  KernelFile     {0}" -f $(if ($kernOk) {"OK"} else {"FAIL"}))
if (-not $kernOk) { $ok = $false }

$sizeMb = [math]::Round((Get-Item $isoFile).Length / 1MB, 2)
Write-Host ""
Write-Host "========================================" -ForegroundColor White
Write-Host (" ISO: {0}" -f $isoFile) -ForegroundColor White
Write-Host (" Size: {0} MB ({1} sectors)" -f $sizeMb, $totalSectors) -ForegroundColor White
Write-Host (" Boot: El Torito No-Emulation @ sector 0x{0:X} (image embeds kernel)" -f $bootLoadSec) -ForegroundColor White
Write-Host "========================================" -ForegroundColor White
if (-not $ok) { throw "ISO self-check FAILED" }
Write-Host "[OK] ISO generated." -ForegroundColor Green
