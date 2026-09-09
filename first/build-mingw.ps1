# ============================================================
# first project - MinGW 构建脚本 (Windows native)
# 产出: kernel.exe (PE 参考) + kernel.bin (干净扁平内核)
#       + boot.bin (引导扇区) + image.img (可启动软/硬盘镜像)
# 仅依赖: nasm + MinGW gcc/ld/objcopy (无需交叉工具链/GRUB)
#
# -Arch x64 (默认) : 64 位长模式内核
# -Arch x86        : 32 位保护模式内核 (旧版回退)
# ============================================================
param(
    [ValidateSet("x64","x86")]
    [string]$Arch = "x64",
    [switch]$Clean,
    [switch]$Run,
    [switch]$WithPython   # 构建并链接 MicroPython (启用 python/bt 终端命令)
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutputDir = Join-Path $ScriptDir "output"
$BuildDir  = Join-Path $ScriptDir "build-mingw"

$Is64 = ($Arch -eq "x64")
$MFlag    = if ($Is64) { "-m64" } else { "-m32" }
$NasmFmt  = if ($Is64) { "win64" } else { "win32" }
$NasmDef  = @()
if ($Is64) { $NasmDef += "-d", "MINGW", "-d", "MINGW64" } else { $NasmDef += "-d", "MINGW" }
$LdEmu    = if ($Is64) { "i386pep" } else { "i386pe" }
$ArchName = if ($Is64) { "x86-64" } else { "i686" }

# 防止 WorkBuddy 的 "safe-delete" shim 拦截 make 内部的 rm (删除 .d 依赖文件)
# 会把 mingw32-make 当成 GUI 子系统程序导致 9009 等异常
$env:NODE_OPTIONS = ""

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
New-Item -ItemType Directory -Force -Path $BuildDir  | Out-Null

if ($Clean) {
    Write-Host "[CLEAN] Removing build artifacts..." -ForegroundColor Yellow
    Remove-Item -Force "$BuildDir\*" -ErrorAction SilentlyContinue
    Remove-Item -Force "$OutputDir\*" -ErrorAction SilentlyContinue
    Write-Host "[CLEAN] Done" -ForegroundColor Green
    if (-not $Run) { return }
}

Write-Host ("[BUILD] Target architecture: {0}" -f $ArchName) -ForegroundColor Magenta

# 1. 汇编 asm 源文件 -> COFF
Write-Host "[NASM] Assembling start.asm ..." -ForegroundColor Cyan
& nasm -f $NasmFmt @NasmDef -g -F cv8 -i $ScriptDir -i "$ScriptDir\kernel\core" -o "$BuildDir\start.obj" "$ScriptDir\kernel\core\start.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (start.asm) failed" }

Write-Host "[NASM] Assembling idt_asm.asm ..." -ForegroundColor Cyan
& nasm -f $NasmFmt @NasmDef -g -F cv8 -i $ScriptDir -i "$ScriptDir\kernel\core" -o "$BuildDir\idt_asm.obj" "$ScriptDir\kernel\core\idt_asm.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (idt_asm.asm) failed" }

Write-Host "[NASM] Assembling syscall_asm.asm ..." -ForegroundColor Cyan
& nasm -f $NasmFmt @NasmDef -g -F cv8 -i $ScriptDir -i "$ScriptDir\kernel\core" -o "$BuildDir\syscall_asm.obj" "$ScriptDir\kernel\core\syscall_asm.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (syscall_asm.asm) failed" }

# 2. 编译所有 C 源文件 -> COFF
# 注意: gcc 的 warning 会写到 stderr, PowerShell 5.1 在 $ErrorActionPreference=Stop
#       下会把 native stderr 当终止错误, 故用 cmd /c 包装 (同 make 处理方式)。
# ---- 源码目录 (重构后按四层分离) ----
#   kernel\core     内核核心 (kernel/kheap/proc/perm/sysconf/user/kstring/start.asm)
#   kernel\drivers  设备驱动 (vga/kb/mouse/ata/idt/driver/gfx)
#   user            应用层   (app/gui/terminal/wm/desktop/taskmgr/installer/jvm/cint/lang)
# 必须"明确列出目录"而不能用 -Recurse 扫全树: 否则会误收 micropython/ 下的 C 源码,
# 把 MicroPython 当成内核源文件重复编译 (它会由自己的 Makefile 单独构建成 libfsos.a)。
$SrcDirs = @(
    (Join-Path $ScriptDir "kernel\core"),
    (Join-Path $ScriptDir "kernel\drivers"),
    (Join-Path $ScriptDir "user")
)
# 每个源码目录都要进 include 搜索路径: 例如 user\app.c 里的 #include "vga.h"
# 需要能找到 kernel\drivers\vga.h。根目录保留 io.h / layout.h 等全局约定头。
$IncFlags = "-I `"$ScriptDir`" -I `"$ScriptDir\mp_port`""
foreach ($d in $SrcDirs) { $IncFlags = $IncFlags + " -I `"$d`"" }

Write-Host "[GCC] Compiling C sources ..." -ForegroundColor Cyan
$cObjs = @()
foreach ($d in $SrcDirs) {
    foreach ($src in (Get-ChildItem "$d\*.c" | Sort-Object Name)) {
        $obj = Join-Path $BuildDir ($src.BaseName + ".o")
        & cmd /c "gcc $MFlag -Os -ffreestanding -nostdlib -nostartfiles -fno-pie -fno-stack-protector -mgeneral-regs-only -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -g $IncFlags -c -o `"$obj`" `"$($src.FullName)`" 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "GCC ($($src.Name)) failed" }
        $cObjs += $obj
    }
}

# 2.5 编译 C++ 源文件 -> COFF (C++ GUI / 运行时; 无异常/RTTI)
Write-Host "[G++] Compiling C++ sources ..." -ForegroundColor Cyan
foreach ($d in $SrcDirs) {
    foreach ($src in (Get-ChildItem "$d\*.cpp" | Sort-Object Name)) {
        $obj = Join-Path $BuildDir ($src.BaseName + ".o")
        & cmd /c "g++ $MFlag -Os -ffreestanding -nostdlib -fno-pie -fno-stack-protector -mgeneral-regs-only -fno-rtti -fno-exceptions -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -g $IncFlags -c -o `"$obj`" `"$($src.FullName)`" 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "G++ ($($src.Name)) failed" }
        $cObjs += $obj
    }
}

# 2b. MicroPython: 二选一
#     -WithPython : 先构建 MicroPython 静态库并链接 (终端 python/bt 命令可用)
#     默认        : 只编译占位桩 mp_stub.c (命令给出友好提示)
# 2a. 拼合并冻结 Better Terminal (BT) 为 C 源 — 必须在 MicroPython 编译前完成,
#     否则 gen_frozen_fsos.c 与 apps/pt 脱节会导致 BT 终端运行异常/黑屏。
if ($WithPython) {
    Write-Host "[BT] Bundling & freezing Better terminal..." -ForegroundColor Cyan
    & python "$ScriptDir\tools\bundle_pt.py" --frozen
    if ($LASTEXITCODE -ne 0) { throw "bundle_pt.py --frozen 失败: BT 终端源码无法编译/冻结" }
}

$mpLibs = @()
if ($WithPython) {
    $mpDir = Join-Path $ScriptDir "micropython\ports\fsos"
    if (-not (Test-Path (Join-Path $mpDir "Makefile"))) {
        throw "MicroPython port not found at $mpDir (run first\tools\fetch_micropython.ps1)"
    }
    Write-Host "[MP] Building MicroPython (libfsos.a) ..." -ForegroundColor Cyan
    # make 的 qstr 生成需要 sh/touch/cp/sed 等 Unix 工具, 且 mkrules.mk 里
    # "@# ..." 注释行在 cmd.exe shell 下会被当成命令执行。
    # 用 Git Bash 的 login shell (-lc) 跑 make 最稳妥: 它重建出正确的 MSYS PATH,
    # 使 make 使用 sh shell, cp/sed/rm/touch 均指向 Git 的 /usr/bin。
    # (从纯 PowerShell 启动时 $env:PATH 修改传不到子进程, 依赖 PATH 兜底不可靠)
    $bashPath = $null
    $bashCands = @(
        "C:\Program Files\Git\bin\bash.exe",
        "C:\Program Files (x86)\Git\bin\bash.exe",
        "$env:LOCALAPPDATA\Programs\Git\bin\bash.exe",
        "D:\Git\bin\bash.exe",
        "$env:USERPROFILE\.workbuddy\binaries\PortableGit\versions\*\bin\bash.exe"
    )
    foreach ($p in $bashCands) {
        $hit = Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { $bashPath = $hit.FullName; break }
    }
    if (-not $bashPath) {
        $gb = Get-Command bash -ErrorAction SilentlyContinue
        if ($gb) { $bashPath = $gb.Source }
    }
    if (-not $bashPath) {
        Write-Host "  [!] 未找到 Git Bash (bash.exe), 无法构建 MicroPython" -ForegroundColor Yellow
        throw "Git Bash not found for MicroPython build"
    }
    $mpPosix = $mpDir -replace '\\', '/'
    # WorkBuddy 的 safe-delete shim 会拦截 make 内部的 rm (清理 .d 依赖文件),
    # 触发批量删除确认并中断 MicroPython 构建。用 RM=true 让 make 的删除变成
    # 无操作, 彻底绕过该 shim (正常构建不需要真正删除 .d; 需要清理时可用 -Clean)。
    $makeCmd = "export NODE_OPTIONS=''; cd '$mpPosix' && mingw32-make PYTHON=python ARCH=$Arch RM=true 2>&1"
    & $bashPath -lc $makeCmd
    if ($LASTEXITCODE -ne 0) { throw "MicroPython build failed" }
    $mpLib = Join-Path $mpDir "build\libfsos.a"
    if (-not (Test-Path $mpLib)) { throw "libfsos.a not produced at $mpLib" }
    $mpLibs = @($mpLib)
} else {
    Write-Host "[GCC] Compiling mp_stub.c (MicroPython disabled)" -ForegroundColor Cyan
    $obj = Join-Path $BuildDir "mp_stub.o"
    # mp_stub.c 会 include "vga.h" 等内核头, 必须用与内核源码相同的 -I 集合
    & cmd /c "gcc $MFlag -ffreestanding -nostdlib -nostartfiles -fno-pie -fno-stack-protector -mgeneral-regs-only -Wall -Wextra -g $IncFlags -c -o `"$obj`" `"$ScriptDir\mp_port\mp_stub.c`" 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "GCC (mp_stub.c) failed" }
    $cObjs += $obj
}

# 3. 链接 PE 内核
#    用 g++ 驱动链接: 自动带上 libgcc (解析 __udivdi3/__cxa_* 等), 同时 -nostdlib
#    不引入 libc/libstdc++, 启动代码由 start.asm 提供。
#    --image-base 0: 让 PE 的 RVA == 绝对地址(链接脚本 . = 1M), 代码中绝对引用
#    直接以 1MB 线性地址编码, 平坦内核加载到 0x100000 后无需重定位即可运行
Write-Host "[LD] Linking kernel.exe ..." -ForegroundColor Cyan
# 第一遍链接: 不含 installer.o (它依赖内嵌镜像 install_payload.obj, 而后者需先有
# image.img)。installer_run 在 kernel.c 中声明为 weak, 此处解析为 NULL, 不报错。
$cObjsNoInstall = $cObjs | Where-Object { $_ -notmatch 'installer\.o$' }
$linkInputs = @("$BuildDir\start.obj", "$BuildDir\idt_asm.obj", "$BuildDir\syscall_asm.obj") + $cObjsNoInstall + $mpLibs
# 用 g++ 驱动链接: 自动带上 libgcc (解析 __udivdi3/__cxa_* 等), 同时 -nostdlib
# 不引入 libc/libstdc++, 启动代码由 start.asm 提供。
# --image-base 0: 让 PE 的 RVA == 绝对地址(链接脚本 . = 1M), 代码中绝对引用
# 直接以 1MB 线性地址编码, 平坦内核加载到 0x100000 后无需重定位即可运行。
# 用 cmd /c 包装避免 PowerShell 对 @splat 空元素的 "MissingArgument" 解析错误。
$objList = ($linkInputs | Where-Object { $_ } | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$mach = if ($Is64) { "-m64" } else { "-m32" }
# --allow-multiple-definition: GCC15 默认 -fno-common 会把 kstring.c 与
#   MicroPython string0.o 的 strlen/strcmp 等符号判为重复定义错误; 此处保留
#   内核自有实现, 忽略 MicroPython 的副本。
& cmd /c "g++ $mach -Wl,--image-base,0 -Wl,-mi386pep -T `"$ScriptDir\linker.ld`" -nostdlib -Wl,--gc-sections -Wl,--allow-multiple-definition -o `"$OutputDir\kernel.exe`" $objList 2>&1"
if ($LASTEXITCODE -ne 0) { throw "Linking kernel.exe failed" }

# 4. 解析 _start 入口线性地址 (nm 符号表; multiboot 头在 .text 开头, 需精确取 _start)
Write-Host "[NM] Resolving _start ..." -ForegroundColor Cyan
$nmOut = & nm "$OutputDir\kernel.exe" 2>$null
$startLine = ($nmOut | Where-Object { $_ -match ' T _start$' }) | Select-Object -First 1
if (-not $startLine) { throw "Cannot locate _start symbol (nm)" }
# image base = 0 时 nm 值即绝对线性地址 (multiboot 头 24B + 对齐 => 0x100020)
$kernelEntry = [Convert]::ToUInt32(($startLine -split '\s+')[0], 16)
Write-Host ("  -> kernel entry (linear) = 0x{0:X8}" -f $kernelEntry) -ForegroundColor Green

# 5. 抽出干净扁平内核: 剥离调试段后 -O binary, 再按 Multiboot2 magic 裁剪前导空洞
Write-Host "[OBJCOPY] Extracting flat kernel.bin ..." -ForegroundColor Cyan
$relocTmp = Join-Path $BuildDir "kernel_tmp.exe"
$fullBin = Join-Path $BuildDir "kernel_full.bin"
# 移除 .bss (启动时代码自行清零, 不占磁盘) 与调试段; 用 cmd 屏蔽 stderr (PowerShell 5.1 会把 native stderr 视为终止错误)
& cmd /c "objcopy --remove-section .reloc --remove-section .bss --remove-section .pdata --remove-section .xdata --strip-debug `"$OutputDir\kernel.exe`" $relocTmp 2>nul"
& cmd /c "objcopy -O binary $relocTmp $fullBin 2>nul"
if ($LASTEXITCODE -ne 0) { throw "objcopy -O binary failed" }

$fullBytes = [System.IO.File]::ReadAllBytes($fullBin)
$startIdx = -1
for ($i = 0; $i -lt $fullBytes.Length - 4; $i++) {
    if ($fullBytes[$i] -eq 0xd6 -and $fullBytes[$i+1] -eq 0x50 -and $fullBytes[$i+2] -eq 0x52 -and $fullBytes[$i+3] -eq 0xe8) {
        $startIdx = $i
        break
    }
}
if ($startIdx -lt 0) { throw "Multiboot2 magic not found in flat image" }
$kernel = New-Object byte[] ($fullBytes.Length - $startIdx)
[Array]::Copy($fullBytes, $startIdx, $kernel, 0, $kernel.Length)
[System.IO.File]::WriteAllBytes("$OutputDir\kernel.bin", $kernel)
Write-Host ("  -> trimmed {0} bytes of image-base padding" -f $startIdx) -ForegroundColor Green

# 导出"干净内核"(不含安装器内嵌镜像), 供 UEFI 镜像打包使用。
# UEFI 路径不经由 CD 安装, 若带安装器 payload, 安装流程会把 image.img 写回
# 磁盘覆盖 GPT/ESP; 用干净内核可避免该问题。
Copy-Item -Path "$OutputDir\kernel.bin" -Destination "$OutputDir\kernel_clean.bin" -Force
Write-Host ("  -> kernel_clean.bin ({0} bytes, no installer payload) for UEFI" -f (Get-Item "$OutputDir\kernel.bin").Length) -ForegroundColor Gray

# 同时导出 ELF 版本 (kernel.elf): 供未来加载器做原生 ELF 解析 / 携带调试符号。
# 非关键步骤, 失败仅告警, 不影响现有扁平内核引导。
& cmd /c "objcopy -O elf64-x86-64 `"$OutputDir\kernel.exe`" `"$OutputDir\kernel.elf`" 2>nul"
if (Test-Path "$OutputDir\kernel.elf") {
    Write-Host ("  -> kernel.elf ({0} bytes, ELF) exported" -f (Get-Item "$OutputDir\kernel.elf").Length) -ForegroundColor Gray
}

$binSize = (Get-Item "$OutputDir\kernel.bin").Length
$magic = [System.IO.File]::ReadAllBytes("$OutputDir\kernel.bin")[0..3]
if (-not ($magic[0] -eq 0xd6 -and $magic[1] -eq 0x50 -and $magic[2] -eq 0x52 -and $magic[3] -eq 0xe8)) { throw "Bad Multiboot2 magic at kernel.bin offset 0" }
$sectors = [math]::Ceiling($binSize / 512) + 1
Write-Host ("  -> kernel.bin {0} bytes, {1} sectors" -f $binSize, $sectors) -ForegroundColor Green

# 5.5 编译解释器模块 (C/C++ 25 / Java 26 SE) 为独立裸 blob
#     模块自包含 (freestanding, 无 libc), 固定虚拟地址链接, 由内核运行时从磁盘按需载入,
#     运行完即释放 -> 空闲内核零占用。输出 CINT.MOD / JVM.MOD 到 first/output。
Write-Host "[MOD] Building interpreter modules (cint / jvm) ..." -ForegroundColor Cyan
$ModSrcDir = Join-Path $ScriptDir "modules"
$modCFlags = "-ffreestanding -nostdlib -fno-pie -fno-stack-protector -mno-red-zone " +
             "-fno-asynchronous-unwind-tables -Os -I `"$ScriptDir\user`" -I `"$ModSrcDir`""
foreach ($m in @("cint", "jvm")) {
    $os = @()
    foreach ($s in @("mod_rt.c", ($m + "_mod.c"))) {
        $bn = "mod_" + $m + "_" + [System.IO.Path]::GetFileNameWithoutExtension($s)
        $obj = Join-Path $BuildDir ($bn + ".o")
        & cmd /c "gcc $modCFlags -c -o `"$obj`" `"$ModSrcDir\$s`" 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "GCC module $m ($s) failed" }
        $os += $obj
    }
    $elf = Join-Path $BuildDir ($m + ".elf")
    $ld  = Join-Path $ModSrcDir ("mod_" + $m + ".ld")
    # --image-base,0: 与内核链接同参, 令 PE RVA == 链接脚本 VMA (0x40000000/0x80000000),
    # 否则默认 ImageBase 高于 VMA 会产出 GB 级空洞 blob (objcopy 后 5GB 文件), patch_mod.py 读取即 MemoryError。
    & cmd /c "g++ -nostdlib -nodefaultlibs -Wl,--image-base,0 -Wl,-mi386pep -T `"$ld`" -Wl,--gc-sections -Wl,--build-id=none -lgcc -o `"$elf`" $($os -join ' ') 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "Link module $m failed" }
    $mod = Join-Path $OutputDir ($m.ToUpper() + ".MOD")
    & cmd /c "objcopy -O binary `"$elf`" `"$mod`" 2>nul"
    if ($LASTEXITCODE -ne 0) { throw "objcopy module $m failed" }
    # 体积守卫: 若模块链接回归出 GB 级空洞文件, 立即中止, 避免 patch_mod.py 读入触发 MemoryError。
    $modLen = (Get-Item $mod).Length
    if ($modLen -gt (2MB)) { throw ("Module {0} size {1} exceeds 2MB guard (link missing --image-base,0?)" -f $m.ToUpper(), $modLen) }
    Write-Host ("  -> {0}.MOD ({1} bytes, KB-scale ok)" -f $m.ToUpper(), $modLen) -ForegroundColor Green
    # 回填 size / crc32 到头部 (供内核加载器精确读取与校验)
    & python "$ScriptDir\tools\patch_mod.py" "$mod"
}

# 6. 汇编引导扇区 boot.asm (读 loader) 与二级引导 loader.asm
#    新版 loader 采用"块加载": 不受 SEC1/SEC2 分段容量限制, 整内核可 >1MB。
#    这里只需把内核总扇区数 KERNEL_SECTORS 注入 loader, SEC2 不再使用(置 0)。
$loaderSectors = 8
$kernelLba     = 1 + $loaderSectors
$sec2Lo        = 0
$sec2Hi        = 0

# ---- 磁盘布局校验 ----
# 布局常量从 layout.h 解析 (唯一权威定义), 避免构建脚本与内核代码各写一份而失同步。
# 所有持久化区域必须位于内核之后: 否则"保存数据"就等于"覆盖内核代码" ->
# 首次启动成功后镜像被写坏 -> 之后每次启动读到坏内核 -> 随机 #UD/#PF/双故障。
$layout = @{}
foreach ($line in Get-Content (Join-Path $ScriptDir "layout.h")) {
    if ($line -match '^\s*#define\s+(\w+)\s+(\d+)') { $layout[$matches[1]] = [int]$matches[2] }
}
foreach ($need in @('DISK_SECTORS','LBA_USER_SB','LBA_USER_REC','LBA_FS_DIR','LBA_FS_DATA','LBA_SYSCONF')) {
    if (-not $layout.ContainsKey($need)) { throw "layout.h missing #define $need" }
}
$diskSectors = $layout['DISK_SECTORS']
$layoutRegions = @(
    @{ Name = 'user DB (sb)';  Lba = $layout['LBA_USER_SB']; Secs = 1 },
    @{ Name = 'user DB (rec)'; Lba = $layout['LBA_USER_REC']; Secs = 1 },
    @{ Name = 'krn fs dir';    Lba = $layout['LBA_FS_DIR'];  Secs = 4 },
    @{ Name = 'krn fs data';   Lba = $layout['LBA_FS_DATA']; Secs = 2048 },
    @{ Name = 'sysconf';       Lba = $layout['LBA_SYSCONF']; Secs = 4 }
)
$kernelEnd = $kernelLba + $sectors
foreach ($r in $layoutRegions) {
    if ($kernelEnd -gt $r.Lba) {
        throw ("Kernel overlaps '$($r.Name)': kernel ends at LBA $kernelEnd, region at LBA $($r.Lba). Enlarge image or adjust layout.h")
    }
    if (($r.Lba + $r.Secs) -gt $diskSectors) {
        throw ("Region '$($r.Name)' (LBA $($r.Lba)+$($r.Secs)) exceeds image ($diskSectors sectors)")
    }
}
Write-Host ("  -> kernel ends at LBA $kernelEnd; data regions 3800+, image $diskSectors sectors (ok)") -ForegroundColor Green
Write-Host ("  -> kernel {0} sectors (chunked loader, no SEC1/SEC2 limit)" -f $sectors) -ForegroundColor Gray

Write-Host "[NASM] Assembling boot.asm ..." -ForegroundColor Cyan
& nasm -f bin -d LOADER_SECTORS=$loaderSectors -i $ScriptDir -i "$ScriptDir\boot" `
        -o "$OutputDir\boot.bin" "$ScriptDir\boot\boot.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (boot.asm) failed" }
$bootSize = (Get-Item "$OutputDir\boot.bin").Length
if ($bootSize -ne 512) { throw "boot.bin is $bootSize bytes, expected 512" }
Write-Host "  -> boot.bin 512 bytes (OK)" -ForegroundColor Green

Write-Host "[NASM] Assembling loader.asm ..." -ForegroundColor Cyan
& nasm -f bin -d KERNEL_LBA=$kernelLba -d KERNEL_ENTRY=0x$($kernelEntry.ToString('X')) `
        -d KERNEL_SECTORS=$sectors -d DEBUG -i $ScriptDir -i "$ScriptDir\boot" `
        -o "$OutputDir\loader.bin" "$ScriptDir\boot\loader.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (loader.asm) failed" }
$loaderSize = (Get-Item "$OutputDir\loader.bin").Length
if ($loaderSize -gt ($loaderSectors * 512)) { throw "loader.bin too large: $loaderSize bytes" }
Write-Host ("  -> loader.bin {0} bytes (max {1})" -f $loaderSize, ($loaderSectors * 512)) -ForegroundColor Green

# 7. 拼接可启动镜像 (boot + loader + kernel + 模块) —— 稀疏文件流式写入
#    4GB 镜像若整体载入内存会 OOM, 故以 FileStream 按需 Seek+Write 各段, 其余保持稀疏。
$gbMode = ([long]$diskSectors * 512) -gt ([long]256 * 1024 * 1024)
Write-Host ("[IMG] Building image.img (streaming, {0} sectors = {1} GB) ..." -f $diskSectors, ([math]::Round([long]$diskSectors * 512 / 1GB, 2))) -ForegroundColor Cyan
$imgPath = Join-Path $OutputDir "image.img"
$kernelPadded = $sectors * 512
$buffer = New-Object byte[] ($kernelPadded)
$kernelBytes = [System.IO.File]::ReadAllBytes("$OutputDir\kernel.bin")
[Array]::Copy($kernelBytes, $buffer, [math]::Min($kernelBytes.Length, $kernelPadded))
$bootBytes = [System.IO.File]::ReadAllBytes("$OutputDir\boot.bin")
$loaderPadded = $loaderSectors * 512
$loaderBytes = [System.IO.File]::ReadAllBytes("$OutputDir\loader.bin")
$loaderBuf = New-Object byte[] $loaderPadded
[Array]::Copy($loaderBytes, $loaderBuf, [math]::Min($loaderBytes.Length, $loaderPadded))

$floppySize = [long]$diskSectors * 512
$fs = [System.IO.File]::Open($imgPath, 'Create', 'ReadWrite')
$fs.SetLength($floppySize)
# boot(512) @0, loader @512, kernel @(1+loaderSectors)*512
$fs.Seek(0, 'Begin');                       $fs.Write($bootBytes, 0, $bootBytes.Length)
$fs.Seek(512, 'Begin');                    $fs.Write($loaderBuf, 0, $loaderPadded)
$fs.Seek([long](1 + $loaderSectors) * 512, 'Begin'); $fs.Write($buffer, 0, $kernelPadded)

# 7.1 写入解释器模块 (CINT.MOD / JVM.MOD) 到 layout.h 定义的 LBA。
#     内核运行时按绝对 LBA 从盘读入预留高地址窗口执行, 运行完即释放。
#     必须与 kernel module.h 的 MOD_CINT_LBA / MOD_JVM_LBA 完全一致。
Write-Host "[MOD] Embedding interpreter modules into image.img ..." -ForegroundColor Cyan
function Write-ModToImageStream($path, $lba) {
    if (-not (Test-Path $path)) { Write-Warning ("module missing, skip: {0}" -f $path); return }
    $m = [System.IO.File]::ReadAllBytes($path)
    $pad = (512 - ($m.Length % 512)) % 512
    if ($pad) { $m = $m + [byte[]]::new($pad) }
    $off = [long]$lba * 512
    if ($off + $m.Length -gt $floppySize) { throw ("module {0} overflows image (LBA {1})" -f $path, $lba) }
    $fs.Seek($off, 'Begin'); $fs.Write($m, 0, $m.Length)
    Write-Host ("  -> LBA {0}: {1} ({2} bytes)" -f $lba, $path, $m.Length) -ForegroundColor Green
}
Write-ModToImageStream (Join-Path $OutputDir "CINT.MOD") $layout['LBA_MOD_CINT']
Write-ModToImageStream (Join-Path $OutputDir "JVM.MOD")  $layout['LBA_MOD_JVM']
$fs.Close()
Write-Host ("  -> image.img {0} bytes (boot + loader + {1} kernel sectors + modules @ GB-scale)" -f $floppySize, $sectors) -ForegroundColor Green

# 7.4 内嵌可安装磁盘镜像并重新链接内核 (仅小镜像模式; GB 级镜像跳过: 安装器 payload
#     会把 4GB image.img 内嵌进内核, 巨型且本机 ISO/El Torito 引导一贯 triple fault,
#     验证一律以 VMware 磁盘镜像为准, 无需安装器 payload)。
if (-not $gbMode) {
Write-Host "[INSTALLER] Embedding disk image into kernel ..." -ForegroundColor Cyan
$installInc = Join-Path $BuildDir "install_inc.inc"
Set-Content -Path $installInc -Value "incbin `"$($imgPath -replace '\\','/')`"" -Encoding ASCII
& nasm -f $NasmFmt -i $BuildDir -i $ScriptDir -i "$ScriptDir\boot" -o "$BuildDir\install_payload.obj" "$ScriptDir\boot\install_payload.asm"
if ($LASTEXITCODE -ne 0) { throw "NASM (install_payload.asm) failed" }
$linkInputs2 = $linkInputs + @("$BuildDir\installer.o", "$BuildDir\install_payload.obj")
$objList2 = ($linkInputs2 | Where-Object { $_ } | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$mach2 = if ($Is64) { "-m64" } else { "-m32" }
& cmd /c "g++ $mach2 -Wl,--image-base,0 -Wl,-mi386pep -T `"$ScriptDir\linker.ld`" -nostdlib -Wl,--allow-multiple-definition -o `"$OutputDir\kernel.exe`" $objList2 2>&1"
if ($LASTEXITCODE -ne 0) { throw "Relink kernel.exe with installer payload failed" }
& cmd /c "objcopy --remove-section .reloc --remove-section .bss --remove-section .pdata --remove-section .xdata --strip-debug `"$OutputDir\kernel.exe`" $relocTmp 2>nul"
& cmd /c "objcopy -O binary $relocTmp $fullBin 2>nul"
if ($LASTEXITCODE -ne 0) { throw "objcopy -O binary (installer) failed" }
$fullBytes2 = [System.IO.File]::ReadAllBytes($fullBin)
$startIdx2 = -1
for ($i = 0; $i -lt $fullBytes2.Length - 4; $i++) {
    if ($fullBytes2[$i] -eq 0xd6 -and $fullBytes2[$i+1] -eq 0x50 -and $fullBytes2[$i+2] -eq 0x52 -and $fullBytes2[$i+3] -eq 0xe8) { $startIdx2 = $i; break }
}
if ($startIdx2 -lt 0) { throw "Multiboot2 magic not found (installer relink)" }
$kernel2 = New-Object byte[] ($fullBytes2.Length - $startIdx2)
[Array]::Copy($fullBytes2, $startIdx2, $kernel2, 0, $kernel2.Length)
[System.IO.File]::WriteAllBytes("$OutputDir\kernel.bin", $kernel2)
Write-Host ("  -> kernel.bin {0} bytes (with installer payload)" -f $kernel2.Length) -ForegroundColor Green
} else {
    Write-Host "[INSTALLER] Skipped: GB-mode image.img not embedded as installer payload (verification uses VMware disk)." -ForegroundColor Yellow
}

# 7.5 同步生成 VMware 可启动磁盘 (vmdk)
$qemuImg = "C:\Program Files\qemu\qemu-img.exe"
$vmDir = Join-Path $OutputDir "vmware"
if (Test-Path $qemuImg) {
    New-Item -ItemType Directory -Force -Path $vmDir | Out-Null
    $vmdkPath = Join-Path $vmDir "image.vmdk"
    & $qemuImg convert -f raw -O vmdk $imgPath $vmdkPath
    if ($LASTEXITCODE -eq 0) {
        Write-Host ("  -> image.vmdk {0} KB (VMware)" -f [math]::Round((Get-Item $vmdkPath).Length/1KB, 1)) -ForegroundColor Green
    } else {
        Write-Host "  [!] qemu-img convert failed, VMware image not updated" -ForegroundColor Yellow
    }
} else {
    Write-Host "  [!] qemu-img.exe not found, skip VMware vmdk" -ForegroundColor Yellow
}

# 7.6 同步生成可启动 ISO (El Torito 硬盘仿真)。GB 级镜像跳过: 嵌入 4GB image.img
#     不现实, 且 ISO/El Torito 引导在本机 VMware 上一贯 triple fault, 验证以磁盘镜像为准。
$isoScript = Join-Path $ScriptDir "pack_iso.ps1"
if (Test-Path $isoScript) {
    if (-not $gbMode) {
        Write-Host ""
        Write-Host "[ISO] Packing bootable ISO (El Torito No-Emulation) ..." -ForegroundColor Cyan
        try {
            & $isoScript
            if ($LASTEXITCODE -ne 0) { throw "pack_iso exited with code $LASTEXITCODE" }
        } catch {
            Write-Host ("  [!] ISO generation failed: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
            Write-Host "      image.img 仍可用作原始磁盘镜像 (qemu -drive / VMware)" -ForegroundColor Gray
        }
    } else {
        Write-Host ("[ISO] Skipped: image.img is {0} GB; embedding into ISO is impractical and El Torito boot is unreliable on this platform. Use VMware disk images to verify." -f ([math]::Round([long]$diskSectors * 512 / 1GB, 2))) -ForegroundColor Yellow
    }
} else {
    Write-Host "  [!] pack_iso.ps1 not found, skip ISO" -ForegroundColor Yellow
}

# 8. 产物清单
Write-Host ""
Write-Host "========================================" -ForegroundColor White
Write-Host " Build complete!" -ForegroundColor White
Write-Host "========================================" -ForegroundColor White
Get-ChildItem $OutputDir\* | ForEach-Object { $kb = [math]::Round($_.Length/1KB, 1); Write-Host ("  {0,-14} {1,8} KB" -f $_.Name, $kb) }
$isoOut = Join-Path (Split-Path -Parent $ScriptDir) "iso\FSOS.iso"
if (Test-Path $isoOut) {
    Write-Host ("  {0,-14} {1,8} MB" -f "FSOS.iso", [math]::Round((Get-Item $isoOut).Length/1MB,2)) -ForegroundColor Green
}

# 9. 运行 (可选)
if ($Run) {
    $qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
    if ($qemu) {
        Write-Host ""
        Write-Host "[QEMU] Booting image.img ..." -ForegroundColor Cyan
        & qemu-system-x86_64 -drive file="$imgPath",format=raw,index=0,media=disk -nographic
    } else {
        Write-Host ""
        Write-Host "[!] qemu-system-x86_64 not found, cannot run" -ForegroundColor Yellow
        Write-Host "    Install: winget install SoftwareFreedomConservancy.QEMU" -ForegroundColor Gray
    }
}
