#!/usr/bin/env python3
"""host_platform.py - 宿主平台差异注入点驱动表 (cross_platform 任务 1.1)

FSOS 构建链对宿主平台的全部差异收敛于此模块: 路径分隔符、NASM 格式/宏/调试格式、
链接模拟格式、objcopy 剥段清单、本地工具调用契约。业务步骤代码不得出现平台条件分支;
新增平台差异仅增补驱动表条目。

平台事实(detect) ->
  PlatformFacts  (family / arch_bits / path_sep / tools)
  PlatformInjector (asm_fmt / asm_defs / nasm_dbg / ld_emu / objcopy_remove / shell)
"""
import os
import platform as _pymod_pl
import shutil
import subprocess
import sys
import warnings

# ---------------------------------------------------------------------------
# 注入点驱动表: (OS family, arch_bits) -> 各注入点取值
#   asm_fmt        NASM 输出格式 (build-mingw.ps1: $NasmFmt)
#   asm_defs       NASM 命令行宏 (build-mingw.ps1: $NasmDef)
#   nasm_dbg       NASM 调试格式 (-F cv8 于 Windows / dwarf 于 POSIX)
#   ld_emu         GNU ld -m 模拟格式 (i386pep / elf_x86_64)
#   objcopy_remove objcopy --remove-section 清单 (PE 需显式剥段, ELF 为空)
#   shell          本地工具调用契约: cmd (/c 包装) 或 posix (直调)
# ---------------------------------------------------------------------------
_INJECT_TABLE = {
    ("windows", 64): dict(
        asm_fmt="win64", asm_defs=["-d", "MINGW", "-d", "MINGW64"],
        nasm_dbg=["-g", "-F", "cv8"], ld_emu="i386pep",
        objcopy_remove=[".reloc", ".bss", ".pdata", ".xdata"],
        shell="cmd", link_image_base=["-Wl,--image-base,0"]),
    ("windows", 32): dict(
        asm_fmt="win32", asm_defs=["-d", "MINGW"],
        nasm_dbg=["-g", "-F", "cv8"], ld_emu="i386pe",
        objcopy_remove=[".reloc", ".bss", ".pdata", ".xdata"],
        shell="cmd", link_image_base=["-Wl,--image-base,0"]),
    ("linux", 64): dict(
        asm_fmt="elf64", asm_defs=[],
        nasm_dbg=["-g", "-F", "dwarf"], ld_emu="elf_x86_64",
        objcopy_remove=[".bss"], shell="posix", link_image_base=[]),
    ("linux", 32): dict(
        asm_fmt="elf32", asm_defs=[],
        nasm_dbg=["-g", "-F", "dwarf"], ld_emu="elf_i386",
        objcopy_remove=[".bss"], shell="posix", link_image_base=[]),
    ("macos", 64): dict(
        asm_fmt="elf64", asm_defs=[],
        nasm_dbg=["-g", "-F", "dwarf"], ld_emu="elf_x86_64",
        objcopy_remove=[".bss"], shell="posix", link_image_base=[]),
    ("macos", 32): dict(
        asm_fmt="elf32", asm_defs=[],
        nasm_dbg=["-g", "-F", "dwarf"], ld_emu="elf_i386",
        objcopy_remove=[".bss"], shell="posix", link_image_base=[]),
}

# 常见宿主工具候选目录 (无需用户配置的默认探测路径)
_QEMU_NAMES = ["qemu-system-x86_64.exe", "qemu-system-x86_64"]
_QEMU_CANDIDATES_WIN = [r"C:\Program Files\qemu", r"C:\Program Files (x86)\qemu"]
_QEMU_CANDIDATES_POSIX = ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/opt/local/bin"]


def _os_family():
    s = _pymod_pl.system().lower()
    if s.startswith("windows") or sys.platform.startswith("win"):
        return "windows"
    if s == "darwin":
        return "macos"
    if s == "linux":
        return "linux"
    warnings.warn("unknown OS %r, treating as POSIX" % s)
    return "linux"


def _machine_arch_bits():
    m = _pymod_pl.machine().lower()
    if m in ("x86_64", "amd64", "x64", "x86-64"):
        return 64
    if m in ("i386", "i486", "i586", "i686", "x86", "athlon"):
        return 32
    warnings.warn("unknown architecture %r, assuming 64-bit" % m)
    return 64


class PlatformFacts:
    """宿主平台事实快照: 家族 / 位数 / 路径分隔 / 发现到的工具路径。"""

    def __init__(self, family, arch_bits, tools=None):
        self.family = family
        self.arch_bits = arch_bits
        self.tools = tools or {}

    @property
    def path_sep(self):
        return "\\" if self.family == "windows" else "/"

    def resolve_tool(self, names, candidates=None, envs=None):
        """三路工具发现: PATH → 候选目录表 → 环境变量 (至少某一路命中即返回)。"""
        names = list(names)
        for n in names:
            p = shutil.which(n)
            if p:
                return p
        cand_dirs = candidates if candidates is not None else (
            _QEMU_CANDIDATES_WIN if self.family == "windows" else _QEMU_CANDIDATES_POSIX)
        for d in cand_dirs:
            for n in names:
                p = os.path.join(d, n)
                if os.path.isfile(p) and os.access(p, os.X_OK):
                    return p
        for env in envs or ():
            v = os.environ.get(env) or ""
            if not v:
                continue
            if os.path.isfile(v):
                return v
            p = os.path.join(v, names[0])
            if os.path.isfile(p):
                return p
        return None

    def as_dict(self):
        return dict(family=self.family, arch_bits=self.arch_bits, tools=dict(self.tools))


def detect(platform=None):
    """探测当前 (或指定) 宿主平台, 返回 PlatformFacts。platform 可为
    None / "windows" / ("macos", 64) 等, 便于单测注入。
    若 platform 为 None 且环境变量 FSOS_PLATFORM_OVERRIDE 已设置, 则使用之。"""
    if platform is None:
        env_override = os.environ.get("FSOS_PLATFORM_OVERRIDE")
        if env_override:
            platform = env_override
    if platform is None:
        family, bits = _os_family(), _machine_arch_bits()
    elif isinstance(platform, tuple):
        family, bits = platform
    elif isinstance(platform, str):
        family, bits = platform, _machine_arch_bits()
    else:
        raise TypeError("platform must be None/str/tuple, got %r" % (platform,))
    facts = PlatformFacts(family, bits)
    qemu_cands = _QEMU_CANDIDATES_WIN if family == "windows" else _QEMU_CANDIDATES_POSIX
    facts.tools = {
        "nasm": facts.resolve_tool(["nasm", "nasm.exe"]),
        "cc": facts.resolve_tool(["gcc", "cc", "x86_64-linux-gnu-gcc", "clang", "gcc.exe"]),
        "cxx": facts.resolve_tool(["g++", "c++", "clang++", "g++.exe"]),
        "nm": facts.resolve_tool(["nm", "x86_64-linux-gnu-nm", "nm.exe"]),
        "objcopy": facts.resolve_tool(["objcopy", "x86_64-linux-gnu-objcopy", "objcopy.exe"]),
        "python": facts.resolve_tool(["python", "python3", "python.exe"]),
        "qemu": facts.resolve_tool(_QEMU_NAMES, candidates=qemu_cands),
    }
    return facts


class PlatformInjector:
    """按平台事实取注入点取值, 供构建/打包/验证全部组件消费 (全项目唯一平台分支点)。"""

    def __init__(self, facts):
        self.facts = facts
        row = _INJECT_TABLE.get((facts.family, facts.arch_bits))
        if row is None:
            row = _INJECT_TABLE[("linux", facts.arch_bits)]
            warnings.warn("no injector row for %s/%d, falling back to POSIX"
                          % (facts.family, facts.arch_bits))
        self.asm_fmt = row["asm_fmt"]
        self.asm_defs = list(row["asm_defs"])
        self.nasm_dbg = list(row["nasm_dbg"])
        self.ld_emu = row["ld_emu"]
        self.objcopy_remove = list(row["objcopy_remove"])
        self.shell = row["shell"]
        self.link_image_base = list(row.get("link_image_base", []))

    def is_windows(self):
        return self.facts.family == "windows"

    def shell_cmd(self, argv, **kw):
        """按 shell 契约执行本地命令: Windows 经 cmd /c 包装 (规避 PowerShell
        把 native stderr 当终止错误的陷阱), POSIX 直接 subprocess 调用。"""
        if self.is_windows():
            return subprocess.run(["cmd", "/c"] + list(argv), **kw)
        return subprocess.run(list(argv), **kw)

    def check_output(self, argv, **kw):
        if self.is_windows():
            return subprocess.check_output(["cmd", "/c"] + list(argv), **kw)
        return subprocess.check_output(list(argv), **kw)

    def as_dict(self):
        return dict(asm_fmt=self.asm_fmt, asm_defs=self.asm_defs, nasm_dbg=self.nasm_dbg,
                    ld_emu=self.ld_emu, objcopy_remove=self.objcopy_remove, shell=self.shell,
                    link_image_base=self.link_image_base)


def new(facts):
    """由 PlatformFacts 构造 PlatformInjector (组装入口, 命名对齐 design)。"""
    return PlatformInjector(facts)