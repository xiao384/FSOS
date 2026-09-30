#!/usr/bin/env python3
"""kernel_build.py - FSOS 内核构建步骤组件与编排器 (cross_platform 组2/3)

将 build-mingw.ps1 的构建流程移植为跨平台步骤: 汇编 → C/C++ 编译 → 链接 →
nm 解析入口 → objcopy 转平坦内核 → 解释器模块 → 引导扇区 → 稀疏镜像拼装。
全部格式类参数经 PlatformInjector 读取 (唯一平台分支点在 host_platform.py)。

承载对象:
  - BuildContext    一次构建的全部上下文 (arch/flags/目录/布局/inject)
  - SectionLog      [SECTION]/[STEP] 分节日志 + [BUILD-FAIL] 错误上下文
  - IncrementalCache mtime + .d 依赖的增量编译判定
  - build_kernel()  build 子命令主入口
"""
import os
import shutil
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402
from layout_parse import load_layout  # noqa: E402
from build_config import BuildConfig  # noqa: E402

EXIT_ARGS = 5
EXIT_TOOLS = 4
EXIT_BUILD = 1
EXIT_VERIFY = 2
EXIT_RUN = 3

ENTRY_MAGIC = bytes([0xD6, 0x50, 0x52, 0xE8])   # Multiboot2 magic 前导空洞裁剪
MOD_SECS = 2097152                              # 模型窗口扇区 (对齐 layout.h)


class BuildError(Exception):
    def __init__(self, step, tool, code):
        super().__init__("[BUILD-FAIL] step=%s tool=%s code=%s" % (step, tool, code))
        self.step, self.tool, self.code = step, tool, code


class SectionLog:
    def __init__(self, platform):
        self.platform = platform
        self._step = None
        self._t0 = 0.0

    def begin(self, sect, step):
        self._step = step
        self._t0 = time.time()
        print("[%s] %s ..." % (sect, step), flush=True)

    def ok(self, msg=""):
        dt = time.time() - self._t0
        print("  -> %s (%.1fs)%s" % (self._step, dt, (" " + msg) if msg else ""), flush=True)


class IncrementalCache:
    def __init__(self, build_dir):
        self.build_dir = build_dir

    @staticmethod
    def _mtime(path):
        try:
            return os.path.getmtime(path)
        except OSError:
            return 0.0

    def _deps(self, dep_file):
        out = []
        try:
            with open(dep_file, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            return out
        for tok in text.replace("\\\n", " ").split():
            if tok.endswith(":") or tok == "-":
                continue
            if os.path.isfile(tok):
                out.append(tok)
        return out

    def is_fresh(self, src, obj):
        if not os.path.isfile(obj):
            return False
        obj_m = self._mtime(obj)
        if obj_m < self._mtime(src):
            return False
        for d in self._deps(obj + ".d"):
            if obj_m < self._mtime(d):
                return False
        return True

    def needs_compile(self, src, obj):
        if self.is_fresh(src, obj):
            print("  [SKIP] %s" % os.path.basename(obj), flush=True)
            return False
        return True


class BuildContext:
    def __init__(self, arch="x64", with_python=False, clean=False, run=False):
        self.arch = arch
        self.with_python = with_python
        self.clean = clean
        self.run = run
        self.facts = detect()
        self.inject = new(self.facts)
        self.layout = load_layout()
        self.is64 = arch == "x64"
        self.arch_bits = 64 if self.is64 else 32
        self.repo = os.path.normpath(os.path.join(
            os.path.dirname(os.path.abspath(__file__)), ".."))
        self.build_dir = os.path.join(self.repo, "build")
        self.output_dir = os.path.join(self.repo, "output")
        self.mflag = "-m64" if self.is64 else "-m32"
        self.loader_sectors = 8

    # ---------------------------------------------------------------
    def compile_flags(self):
        cfg = BuildConfig
        arch = cfg.flags_arch.get(self.arch_bits, cfg.flags_arch[64])
        return (self.mflag + " " + arch + " " + cfg.flags_common).split()

    def include_flags(self):
        dirs = [self.repo,
                os.path.join(self.repo, "mp_port"),
                os.path.join(self.repo, "kernel", "core"),
                os.path.join(self.repo, "kernel", "drivers"),
                os.path.join(self.repo, "user")]
        out = []
        for d in dirs:
            out += ["-I", d]
        return out

    def ensure_dirs(self):
        for d in (self.build_dir, self.output_dir):
            os.makedirs(d, exist_ok=True)

    def clean_dirs(self):
        for d in (self.build_dir, self.output_dir):
            if os.path.isdir(d):
                shutil.rmtree(d, ignore_errors=True)
        # 兼容旧 build-mingw/ 目录 (迁移期双清)
        legacy = os.path.join(self.repo, "build-mingw")
        if os.path.isdir(legacy):
            shutil.rmtree(legacy, ignore_errors=True)
        self.ensure_dirs()


# ---------------------------------------------------------------------------
# 本地命令执行 (经 inject.shell 契约; 失败抛 BuildError)
# ---------------------------------------------------------------------------
def _run(inject, argv, step, tool, capture=False, env=None):
    cp = inject.shell_cmd(argv, capture_output=capture, env=env)
    if cp.returncode != 0:
        if capture and cp.stderr:
            sys.stderr.write(cp.stderr.decode(errors="replace"))
        raise BuildError(step, tool, cp.returncode)
    return cp


# ---------------------------------------------------------------------------
# 步骤 1: 汇编 (build-mingw.ps1:48-59)
# ---------------------------------------------------------------------------
def step_asm(ctx, log, cache, name, src, obj):
    inj = ctx.inject
    argv = (["nasm", "-f", inj.asm_fmt] + inj.asm_defs + inj.nasm_dbg +
            ["-i", ctx.repo, "-i", os.path.join(ctx.repo, "kernel", "core"),
             "-o", obj, src])
    if not cache.needs_compile(src, obj):
        return obj
    log.begin("ASM", name + ".asm")
    _run(inj, argv, name + ".asm", "nasm")
    log.ok(os.path.basename(obj))
    return obj


# ---------------------------------------------------------------------------
# 步骤 2: C/C++ 编译 (白名单目录 + .d 依赖, build-mingw.ps1:80-100)
# ---------------------------------------------------------------------------
def _compile(ctx, log, cache, src, obj, cxx):
    tool, exe = ("g++", ctx.facts.tools.get("cxx") or "g++") if cxx else \
                ("gcc", ctx.facts.tools.get("cc") or "gcc")
    argv = ([exe] + ctx.compile_flags() +
            (BuildConfig.flags_cxx_extra.split() if cxx else []) +
            ["-MMD", "-MF", obj + ".d"] + ctx.include_flags() +
            ["-c", "-o", obj, src])
    if not cache.needs_compile(src, obj):
        return obj
    log.begin(tool, os.path.basename(src))
    _run(ctx.inject, argv, os.path.basename(src), tool)
    log.ok(os.path.basename(obj))
    return obj


def step_c(ctx, log, cache, src, obj):
    return _compile(ctx, log, cache, src, obj, cxx=False)


def step_cpp(ctx, log, cache, src, obj):
    return _compile(ctx, log, cache, src, obj, cxx=True)


# ---------------------------------------------------------------------------
# 步骤 3: 链接内核 (build-mingw.ps1:169-185)
# ---------------------------------------------------------------------------
def step_link_kernel(ctx, log, objs):
    inj = ctx.inject
    argv = (["g++", ctx.mflag] + inj.link_image_base +
            ["-Wl,-m" + inj.ld_emu, "-T", os.path.join(ctx.repo, "linker.ld"),
             "-nostdlib", "-Wl,--gc-sections", "-Wl,--allow-multiple-definition",
             "-lgcc", "-o", os.path.join(ctx.output_dir, "kernel.exe")] + objs)
    log.begin("LD", "Linking kernel.exe")
    _run(inj, argv, "link_kernel", "g++")
    log.ok("kernel.exe")
    return os.path.join(ctx.output_dir, "kernel.exe")


# ---------------------------------------------------------------------------
# 步骤 4: nm 解析 _start (build-mingw.ps1:187-194)
# ---------------------------------------------------------------------------
def step_resolve_start(ctx, log, kernel_exe):
    inj = ctx.inject
    nm = inj.facts.tools.get("nm") or "nm"
    cp = inj.shell_cmd([nm, kernel_exe], capture_output=True)
    entry = None
    for line in (cp.stdout or "").decode(errors="replace").splitlines():
        p = line.split()
        if len(p) >= 3 and p[1] == "T" and p[2] == "_start":
            entry = int(p[0], 16)
            break
    if entry is None:
        raise BuildError("resolve_start", "nm", 1)
    log.begin("NM", "Resolving _start")
    log.ok("kernel entry (linear) = 0x%X" % entry)
    return entry


# ---------------------------------------------------------------------------
# 步骤 5: objcopy 剥段 -> 平坦内核 -> 魔数裁剪 (build-mingw.ps1:196-217)
# ---------------------------------------------------------------------------
def step_extract_kernel(ctx, log, kernel_exe, tmpdir):
    inj = ctx.inject
    reloc = os.path.join(tmpdir, "kernel_tmp.exe")
    full = os.path.join(tmpdir, "kernel_full.bin")
    out = os.path.join(ctx.output_dir, "kernel.bin")
    argv = ["objcopy"]
    for sec in inj.objcopy_remove:
        argv += ["--remove-section", sec]
    argv += ["--strip-debug", kernel_exe, reloc]
    _run(inj, argv, "objcopy_strip", "objcopy")
    _run(inj, ["objcopy", "-O", "binary", reloc, full], "objcopy_bin", "objcopy")
    with open(full, "rb") as fh:
        data = fh.read()
    idx = data.find(ENTRY_MAGIC)
    if idx < 0:
        raise BuildError("trim_magic", "objcopy", 1)
    with open(out, "wb") as fh:
        fh.write(data[idx:])
    clean = os.path.join(ctx.output_dir, "kernel_clean.bin")
    shutil.copyfile(out, clean)
    elf = os.path.join(ctx.output_dir, "kernel.elf")
    try:
        shutil.copyfile(kernel_exe, elf)
    except OSError:
        pass
    log.begin("OBJCOPY", "Extracting flat kernel.bin")
    log.ok("%d bytes loaded" % (len(data) - idx))
    return out, len(data) - idx


# ---------------------------------------------------------------------------
# 步骤 2b: MicroPython 子构建 (桩路径或 --with-python 完整构建)
# ---------------------------------------------------------------------------
def _mp_make_env(ctx):
    """MicroPython 子构建在 Windows 上需要注入的环境:
    - makefile (mkenv.mk) 写死 PYTHON=python3, 本机无 python3 且 Store 的 python
      别名在 make 子进程里无法激活, 故用 `py` 启动器 (C:\\Windows\\py.exe, 真实 exe) 覆盖;
    - 依赖 touch/sed/cat 等 Unix 工具, 注入 Git for Windows 的 usr/bin 到 PATH。"""
    import shutil as _sh
    env = os.environ.copy()
    extra = []
    if ctx.inject.is_windows():
        extra = ["PYTHON=py"]
        if _sh.which("touch") is None:
            for cand in ("D:/Git/usr/bin", "C:/Program Files/Git/usr/bin",
                        "C:/Git/usr/bin", "D:/Program Files/Git/usr/bin"):
                if os.path.isfile(os.path.join(cand, "touch.exe")):
                    env["PATH"] = cand + os.pathsep + env.get("PATH", "")
                    break
    return env, extra


def step_micropython(ctx, log, cache, tmp):
    """无 --with-python 时编译 mp_stub.c 轻量桩; 有 --with-python 时调用
    bundle_pt.py --frozen + make 构建 MicroPython 子项目。端口缺失给出指引并中止。"""
    if not ctx.with_python:
        stub_c = os.path.join(ctx.repo, "mp_port", "mp_stub.c")
        obj = os.path.join(tmp, "mp_stub.o")
        step_c(ctx, log, cache, stub_c, obj)
        return [obj]

    port_dir = os.path.join(ctx.repo, "micropython", "ports", "fsos")
    if not os.path.isdir(port_dir):
        print("[MP] micropython/ports/fsos not found")
        print("[MP] 拉取指引: cd first/micropython && git submodule update --init ports/fsos")
        raise BuildError("mp_port_missing", "micropython", 1)

    bundle = os.path.join(ctx.repo, "tools", "bundle_pt.py")
    if os.path.isfile(bundle):
        log.begin("MP", "bundle_pt.py --frozen")
        _run(ctx.inject, [sys.executable, bundle, "--frozen"], "bundle_pt", "python")
        log.ok()

    log.begin("MP", "Building MicroPython (make)")
    make = ctx.facts.tools.get("make") or "mingw32-make"
    if ctx.inject.is_windows():
        for cand in ("mingw32-make", "make"):
            p = ctx.facts.resolve_tool([cand])
            if p:
                make = p
                break
    mp_env, mp_extra = _mp_make_env(ctx)
    _run(ctx.inject, [make, "-C", port_dir] + mp_extra, "mp_build", "make", env=mp_env)
    log.ok()

    mp_build = os.path.join(port_dir, "build")
    objs = []
    if os.path.isdir(mp_build):
        for fn in sorted(os.listdir(mp_build)):
            if fn.endswith(".a"):
                objs.append(os.path.join(mp_build, fn))
    if not objs:
        lib = os.path.join(mp_build, "libfsos.a")
        if os.path.isfile(lib):
            objs.append(lib)
    if not objs:
        raise BuildError("mp_no_output", "make", 1)
    log.ok("MicroPython objects: %d" % len(objs))
    return objs


# ---------------------------------------------------------------------------
# 步骤 6: 解释器模块 (build-mingw.ps1:238-264 语义)
# ---------------------------------------------------------------------------
def step_modules(ctx, log, tmpdir):
    inj = ctx.inject
    mod_src = os.path.join(ctx.repo, "modules")
    outputs = {}
    for m in ("cint", "jvm"):
        objs = []
        for s in ("mod_rt.c", m + "_mod.c"):
            obj = os.path.join(tmpdir, "mod_%s_%s.o" % (m, os.path.splitext(s)[0]))
            argv = (["gcc", "-ffreestanding", "-nostdlib", "-fno-pie",
                     "-fno-stack-protector", "-mno-red-zone",
                     "-fno-asynchronous-unwind-tables", "-Os",
                     "-I", os.path.join(ctx.repo, "user"),
                     "-I", mod_src, "-c", "-o", obj,
                     os.path.join(mod_src, s)])
            log.begin("MOD", "%s (%s)" % (m, s))
            _run(inj, argv, "mod_%s_%s" % (m, s), "gcc")
            log.ok()
            objs.append(obj)
        elf = os.path.join(tmpdir, m + ".elf")
        ld = os.path.join(mod_src, "mod_%s.ld" % m)
        argv = (["g++", "-nostdlib", "-nodefaultlibs"] + inj.link_image_base +
                ["-Wl,-m" + inj.ld_emu, "-T", ld, "-Wl,--gc-sections",
                 "-Wl,--build-id=none", "-lgcc", "-o", elf] + objs)
        log.begin("MOD", "Linking %s module" % m)
        _run(inj, argv, "link_mod_" + m, "g++")
        mod_path = os.path.join(ctx.output_dir, m.upper() + ".MOD")
        _run(inj, ["objcopy", "-O", "binary", elf, mod_path],
             "objcopy_mod_" + m, "objcopy")
        size = os.path.getsize(mod_path)
        if size > BuildConfig.MOD_MAX_BYTES:
            raise BuildError("guard_mod_%s" % m, "objcopy", size)
        _patch_mod(ctx, mod_path)
        outputs[m.upper()] = mod_path
        log.ok("%s.MOD (%d bytes, KB-scale ok)" % (m.upper(), size))
    return outputs


def _patch_mod(ctx, mod_path):
    """回填 size/crc32 到模块头 (与 tools/patch_mod.py 同语义, 直接内联避免子进程)。"""
    with open(mod_path, "r+b") as fh:
        head = fh.read(20)
        if len(head) < 20:
            return
        size = os.path.getsize(mod_path)
        fh.seek(12)
        fh.write(struct.pack("<I", size))
        with open(mod_path, "rb") as rf:
            crc = zlib.crc32(rf.read()) & 0xFFFFFFFF
        fh.seek(16)
        fh.write(struct.pack("<I", crc))


# ---------------------------------------------------------------------------
# 步骤 7: boot.bin / loader.bin (build-mingw.ps1:305-320)
# ---------------------------------------------------------------------------
def step_boot_sectors(ctx, log, kernel_lba, entry, sectors, out_dir):
    aix = ctx.inject
    boot_asm = os.path.join(ctx.repo, "boot", "boot.asm")
    loader_asm = os.path.join(ctx.repo, "boot", "loader.asm")
    boot = os.path.join(out_dir, "boot.bin")
    loader = os.path.join(out_dir, "loader.bin")
    _run(aix, ["nasm", "-f", "bin", "-d", "LOADER_SECTORS=%d" % ctx.loader_sectors,
               "-i", ctx.repo, "-i", os.path.join(ctx.repo, "boot"),
               "-o", boot, boot_asm], "boot.asm", "nasm")
    if os.path.getsize(boot) != BuildConfig.BOOT_SECTOR_BYTES:
        raise BuildError("boot_size", "nasm", os.path.getsize(boot))
    _run(aix, ["nasm", "-f", "bin", "-d", "KERNEL_LBA=%d" % kernel_lba,
               "-d", "KERNEL_ENTRY=0x%X" % entry,
               "-d", "KERNEL_SECTORS=%d" % sectors,
               "-d", "DEBUG", "-i", ctx.repo, "-i", os.path.join(ctx.repo, "boot"),
               "-o", loader, loader_asm], "loader.asm", "nasm")
    log.begin("NASM", "Assembling boot.asm / loader.asm")
    log.ok("boot.bin %dB, loader.bin %dB" %
           (os.path.getsize(boot), os.path.getsize(loader)))
    return boot, loader


# ---------------------------------------------------------------------------
# 步骤 8: 稀疏拼装 image.img (build-mingw.ps1:322-362)
# ---------------------------------------------------------------------------
def step_assemble_image(ctx, log, layout, kernel_bin, boot_bin, loader_bin, modules):
    ker = os.path.join(ctx.output_dir, "kernel.bin")
    ksize = os.path.getsize(ker)
    sectors = (ksize + 511) // 512 + 1
    kernel_lba = 1 + ctx.loader_sectors
    end = kernel_lba + sectors
    disk_sectors = layout.DISK_SECTORS
    floppy_size = disk_sectors * 512
    img = os.path.join(ctx.output_dir, "image.img")

    regions = (("user sb", layout.LBA_USER_SB, 1),
               ("user rec", layout.LBA_USER_REC, 1),
               ("fs dir", layout.LBA_FS_DIR, 4),
               ("fs data", layout.LBA_FS_DATA, 2048),
               ("sysconf", layout.LBA_SYSCONF, 4))
    for name, lba, secs in regions:
        if end > lba:
            raise BuildError("layout_kernel_overlap_%s" % name, "layout", end)

    with open(kernel_bin, "rb") as fh:
        kb = fh.read()
    with open(boot_bin, "rb") as fh:
        bb = fh.read()
    with open(loader_bin, "rb") as fh:
        lb = fh.read()

    log.begin("IMG", "Building image.img (streaming %d sectors)" % disk_sectors)
    with open(img, "wb") as fh:
        fh.truncate(floppy_size)
        fh.seek(0)
        fh.write(bb)
        fh.seek(512)
        fh.write(lb.ljust(ctx.loader_sectors * 512, b"\0")[:ctx.loader_sectors * 512])
        fh.seek(kernel_lba * 512)
        pad = sectors * 512 - len(kb)
        fh.write(kb + (b"\0" * pad if pad > 0 else b""))
        for key, path in (("CINT", "CINT.MOD"), ("JVM", "JVM.MOD")):
            if path not in modules:
                continue
            lba = layout["LBA_MOD_" + key]
            with open(modules[path], "rb") as mf:
                m = mf.read()
            m = m.ljust((len(m) + 511) // 512 * 512, b"\0")
            off = lba * 512
            if off + len(m) > floppy_size:
                raise BuildError("module_overflow_" + key, "image", lba)
            fh.seek(off)
            fh.write(m)
            log.ok("LBA %d: %s (%d bytes)" % (lba, path, len(m)))
    log.ok("image.img %d bytes (boot + loader + %d kernel sectors + modules)"
           % (floppy_size, sectors))
    return img, sectors, end


# ---------------------------------------------------------------------------
# build 子命令主入口
# ---------------------------------------------------------------------------
def build_kernel(ctx):
    ctx.ensure_dirs()
    if ctx.clean:
        ctx.clean_dirs()
    log = SectionLog(ctx.facts)
    cache = IncrementalCache(ctx.build_dir)
    tmp = ctx.build_dir

    # 1. 汇编
    core = os.path.join(ctx.repo, "kernel", "core")
    objs = []
    for n in ("start", "idt_asm", "syscall_asm"):
        objs.append(step_asm(ctx, log, cache, n,
                             os.path.join(core, n + ".asm"),
                             os.path.join(tmp, n + ".obj")))

    # 2. 白名单目录 C/C++ 编译
    for d in ("kernel/core", "kernel/drivers", "user"):
        dpath = os.path.join(ctx.repo, d)
        for fn in sorted(os.listdir(dpath)):
            s = os.path.join(dpath, fn)
            if not os.path.isfile(s):
                continue
            if fn.endswith(".cpp"):
                objs.append(step_cpp(ctx, log, cache, s,
                                     os.path.join(tmp, os.path.splitext(fn)[0] + ".o")))
            elif fn.endswith(".c"):
                objs.append(step_c(ctx, log, cache, s,
                                   os.path.join(tmp, os.path.splitext(fn)[0] + ".o")))

    # 2b. MicroPython: 桩路径或完整 --with-python 构建
    mp_objs = step_micropython(ctx, log, cache, tmp)
    objs.extend(mp_objs)

    # 3-5. 链接 + 解析入口 + 转平坦内核
    kernel_exe = step_link_kernel(ctx, log, objs)
    entry = step_resolve_start(ctx, log, kernel_exe)
    kernel_bin, ksize = step_extract_kernel(ctx, log, kernel_exe, tmp)

    # 6-8. 模块 + 引导扇区 + 镜像
    modules = step_modules(ctx, log, tmp)
    sectors = (ksize + 511) // 512 + 1
    boot, loader = step_boot_sectors(ctx, log, 1 + ctx.loader_sectors, entry, sectors,
                                     ctx.output_dir)
    img, sectors, end = step_assemble_image(ctx, log, ctx.layout, kernel_bin, boot, loader,
                                            modules)

    print("  -> kernel ends at LBA %d; data regions %d+, image %d sectors (ok)"
          % (end, ctx.layout.LBA_USER_SB, ctx.layout.DISK_SECTORS))

    from artifact_check import ArtifactReport
    report = ArtifactReport()
    if not report.verify_build_outputs(ctx.output_dir, ctx.layout.DISK_SECTORS):
        print(report.summary()[0], file=sys.stderr)
        raise BuildError("artifact_check", "verify", EXIT_VERIFY)
    print(report.summary()[0])

    print("Build complete!")
    return dict(image=img, kernel=kernel_bin, sectors=sectors, end_lba=end, entry=entry)