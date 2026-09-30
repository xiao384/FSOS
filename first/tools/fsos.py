#!/usr/bin/env python3
"""fsos.py - FSOS 跨平台统一构建驱动 (cross_platform 组2.1 / 组6 checkenv)

单入口提供 build / verify / pack / checkenv / clean / run 六个子命令, 参数语义与
build-mingw.ps1 一一对应。退出码分层 (spec 6.1.4):
  0 成功
  1 构建失败       2 产物校验失败   3 运行验证失败
  4 工具链缺失      5 参数非法
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402
from build_config import BuildConfig  # noqa: E402
from kernel_build import (  # noqa: E402
    BuildContext, BuildError, build_kernel,
    EXIT_ARGS, EXIT_TOOLS, EXIT_BUILD, EXIT_VERIFY, EXIT_RUN)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))


# ---------------------------------------------------------------------------
# checkenv (组6): 工具链四要素自检 (经 env_report.py)
# ---------------------------------------------------------------------------
def cmd_checkenv(args):
    try:
        from env_report import generate_report
        text, ok = generate_report(json_output=getattr(args, "json", False))
        print(text)
        return 0 if ok else EXIT_TOOLS
    except ImportError:
        return _cmd_checkenv_fallback()


def _cmd_checkenv_fallback():
    facts = detect()
    print("[checkenv] host: %s/%d" % (facts.family, facts.arch_bits))
    missing = []
    for name, names, desc, tool_key in _CHECKLIST:
        path = facts.tools.get(tool_key) or facts.resolve_tool(names)
        if path:
            print("  [OK] %-8s %s" % (name, path))
        else:
            print("  [MISSING] %-8s %s" % (name, desc))
            missing.append(name)
    if missing:
        print("  install: %s" % ", ".join(missing))
        return EXIT_TOOLS
    print("[checkenv] all toolchain present")
    return 0


_CHECKLIST = [
    ("nasm", BuildConfig.NASM_NAMES, "NASM >= %s" % BuildConfig.NASM_MIN, "nasm"),
    ("cc", BuildConfig.CC_NAMES, "GCC >= %s" % BuildConfig.GCC_MIN, "cc"),
    ("cxx", BuildConfig.CXX_NAMES, "C++ 编译器 (g++/clang++)", "cxx"),
    ("python", ["python", "python3"], "Python >= %s" % BuildConfig.PYTHON_MIN, "python"),
    ("qemu", BuildConfig.QEMU_NAMES, "QEMU >= %s" % BuildConfig.QEMU_MIN, "qemu"),
]


# ---------------------------------------------------------------------------
# clean (组6): 清理构建产物 + 关键路径安全校验
# ---------------------------------------------------------------------------
def cmd_clean(args):
    for guard in ("kernel", "linker.ld", "boot"):
        gp = os.path.join(REPO, guard)
        if not os.path.exists(gp):
            print("[clean] SAFETY CHECK FAILED: %s not found (not a FSOS repo?)" % gp,
                  file=sys.stderr)
            return EXIT_ARGS
    ctx = BuildContext(arch=args.arch)
    ctx.clean_dirs()
    print("[clean] removed build/output dirs (host toolchain untouched)")
    return 0


# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------
def cmd_build(args):
    ctx = BuildContext(arch=args.arch, with_python=args.with_python, clean=args.clean)
    try:
        result = build_kernel(ctx)
    except BuildError as e:
        print(str(e), file=sys.stderr)
        return EXIT_BUILD
    if args.run:
        return cmd_run_impl(os.path.join(REPO, "output", "image.img"))
    return 0


# ---------------------------------------------------------------------------
# run / verify: 实际逻辑在组5 verify_boot.py; 此处加载并转发
# ---------------------------------------------------------------------------
def _load_verify():
    sys.path.insert(0, HERE)
    from verify_boot import verify_image  # noqa: F401
    return verify_image


def cmd_run_impl(img):
    try:
        verify_image = _load_verify()
    except Exception:
        print("[run] verify_boot.py 未实现 (组5), 直接调用 QEMU")
        qemu = detect().tools.get("qemu") or "qemu-system-x86_64"
        return subprocess.run([qemu, "-drive",
                               "file=%s,format=raw,index=0,media=disk"
                               % img.replace("\\", "/")]).returncode
    rc = verify_image(img, timeout=120, with_gdb=False)
    return 0 if rc is None else (EXIT_RUN if rc else 0)


def cmd_run(args):
    img = os.path.join(REPO, "output", "image.img")
    if not os.path.isfile(img):
        print("[run] missing %s (run fsos.py build first)" % img, file=sys.stderr)
        return EXIT_BUILD
    return cmd_run_impl(img)


def cmd_verify(args):
    img = args.path if os.path.isabs(args.path) else os.path.join(REPO, args.path)
    if not os.path.isfile(img):
        print("[verify] missing %s" % img, file=sys.stderr)
        return EXIT_VERIFY
    try:
        verify_image = _load_verify()
    except Exception:
        print("[verify] verify_boot.py 未实现 (组5), 暂以哨兵扫描代位", file=sys.stderr)
        return _fallback_sentinel(img)
    rc = verify_image(img, timeout=args.timeout, mode=args.mode,
                      smoke=args.smoke, with_gdb=(not args.no_gdb))
    return 0 if rc is None else (EXIT_RUN if rc else 0)


def _fallback_sentinel(img):
    import tempfile
    serf = os.path.join(tempfile.gettempdir(), "fsos_verify.log")
    qemu = detect().tools.get("qemu") or "qemu-system-x86_64"
    p = subprocess.Popen([qemu, "-drive",
                          "file=%s,format=raw" % img.replace("\\", "/"),
                          "-serial", "file:" + serf, "-m", "1G", "-display", "none"])
    import time
    time.sleep(15)
    p.terminate()
    try:
        data = open(serf, "rb").read()
    except OSError:
        data = b""
    ok = b"FSOS_BOOT_OK" in data
    print("[verify] FSOS_BOOT_OK present: %s" % ok)
    return 0 if ok else EXIT_VERIFY


# ---------------------------------------------------------------------------
# VMware VMX validation/fix
# 打包器生成后统一校正 VMware 启动项，避免 VMDK 被错误声明为 CDROM。
# ---------------------------------------------------------------------------
def _fix_vmware_vmx():
    import re
    roots = [os.path.join(REPO, "output", "Auto"),
             os.path.join(REPO, "output")]
    paths = []
    seen = set()
    for root in roots:
        if not os.path.isdir(root):
            continue
        for name in os.listdir(root):
            if name.lower().endswith(".vmx"):
                path = os.path.join(root, name)
                if path not in seen:
                    seen.add(path)
                    paths.append(path)

    def set_key(data, key, value):
        import re
        pat = re.compile(r"(?m)^" + re.escape(key) + r"\s*=.*$")
        line = '%s = "%s"' % (key, value)
        if pat.search(data):
            return pat.sub(line, data)
        if not data.endswith("\n"):
            data += "\n"
        return data + line + "\n"

    fixed = 0
    for path in paths:
        try:
            data = open(path, "r", encoding="utf-8", errors="replace").read()
        except OSError as exc:
            print("[VMX] read failed: %s" % exc, file=sys.stderr)
            continue
        original = data
        data = set_key(data, "firmware", "efi")
        data = set_key(data, "numvcpus", "2")
        data = set_key(data, "cpuid.coresPerSocket", "2")
        data = set_key(data, "ide0:0.present", "TRUE")
        data = set_key(data, "ide0:0.deviceType", "disk")
        data = set_key(data, "ide0:0.mode", "persistent")
        data = set_key(data, "ide0:0.startConnected", "TRUE")
        data = set_key(data, "bios.bootOrder", "ide0:0")
        data = set_key(data, "efi.quickBoot.enabled", "FALSE")
        if not re.search(r'(?m)^ide0:0\\.fileName\\s*=', data):
            data = set_key(data, "ide0:0.fileName", "FSOS_UEFI.vmdk")
        if data != original:
            Path = __import__("pathlib").Path
            Path(path).write_text(data, encoding="utf-8")
            fixed += 1
            print("[VMX] fixed: %s" % path)
    if paths:
        print("[VMX] checked=%d fixed=%d" % (len(paths), fixed))
    return fixed


# ---------------------------------------------------------------------------
# pack (组4); 未实现时给出指引
# ---------------------------------------------------------------------------
def cmd_pack(args):
    rc = 0
    if args.uefi or args.all:
        try:
            from pack_uefi import pack_uefi_main
            rc = pack_uefi_main(args) or rc
        except ImportError:
            print("[pack] pack_uefi.py 未实现", file=sys.stderr)
            if not args.iso:
                return EXIT_ARGS
    if args.iso or args.all or (not args.uefi and not args.vmdk):
        try:
            from build_iso import pack_main
            rc = pack_main(args) or rc
        except ImportError:
            print("[pack] build_iso.py 未实现", file=sys.stderr)
            if not args.uefi:
                return EXIT_ARGS
    if args.uefi or args.all:
        _fix_vmware_vmx()
    return rc


# ---------------------------------------------------------------------------
# 平台覆盖映射 (design §2.4.2 阶段 C)
# ---------------------------------------------------------------------------
_PLATFORM_MAP = {"win32": "windows", "darwin": "macos", "linux": "linux"}


def _resolve_platform(override):
    """将 --platform 参数映射为 detect() 可接受的 family 字符串。"""
    if not override:
        return None
    return _PLATFORM_MAP.get(override, override)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="fsos.py",
        description="FSOS 跨平台统一构建驱动 (与 build-mingw.ps1 参数语义一致)")

    # 全局 --platform 参数 (design §2.4.2 阶段 C: 显式覆盖探测值)
    ap.add_argument("--platform", choices=["win32", "darwin", "linux"], default=None,
                    help="显式覆盖宿主平台探测 (win32/darwin/linux)")

    sub = ap.add_subparsers(dest="cmd", required=True)

    pb = sub.add_parser("build", help="构建可引导镜像 (boot+loader+kernel+模块)")
    pb.add_argument("--arch", choices=["x64", "x86"], default="x64")
    pb.add_argument("--with-python", action="store_true", help="构建并链接 MicroPython")
    pb.add_argument("--clean", action="store_true", help="清理中间/产物目录后全量构建")
    pb.add_argument("--run", action="store_true", help="构建完成后直接启动 QEMU")
    pb.set_defaults(func=cmd_build)

    pv = sub.add_parser("verify", help="QEMU 启动验证 (FSOS_BOOT_OK 哨兵断言)")
    pv.add_argument("path", nargs="?", default="output/image.img")
    pv.add_argument("--mode", choices=["auto", "drive", "cdrom", "uefi"], default="auto")
    pv.add_argument("--smoke", action="store_true")
    pv.add_argument("--timeout", type=int, default=25)
    pv.add_argument("--no-gdb", action="store_true")
    pv.set_defaults(func=cmd_verify)

    pp = sub.add_parser("pack", help="打包 ISO / UEFI / vmdk 产物")
    pp.add_argument("--iso", action="store_true")
    pp.add_argument("--uefi", action="store_true")
    pp.add_argument("--vmdk", action="store_true")
    pp.add_argument("--all", action="store_true")
    pp.add_argument("--mode", choices=["noemul", "floppy"], default="noemul")
    pp.set_defaults(func=cmd_pack)

    pc = sub.add_parser("checkenv", help="工具链四要素自检")
    pc.set_defaults(func=cmd_checkenv)

    pl = sub.add_parser("clean", help="清理构建中间/产物目录")
    pl.add_argument("--arch", choices=["x64", "x86"], default="x64")
    pl.set_defaults(func=cmd_clean)

    pr = sub.add_parser("run", help="用 QEMU 运行最近构建的 image.img")
    pr.set_defaults(func=cmd_run)

    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        if e.code == 2:
            return EXIT_ARGS
        raise
    if not hasattr(args, "cmd") or not hasattr(args, "func"):
        ap.print_help()
        return EXIT_ARGS
    # 将 --platform 覆盖注入环境, 供 host_platform.detect() 读取
    _pf = _resolve_platform(args.platform)
    if _pf:
        os.environ["FSOS_PLATFORM_OVERRIDE"] = _pf
    try:
        return args.func(args)
    except SystemExit as e:
        if e.code == 2:
            return EXIT_ARGS
        raise
    except Exception as e:   # 未归类异常 -> 构建失败
        print("[fsos] unhandled error: %r" % (e,), file=sys.stderr)
        return EXIT_BUILD


if __name__ == "__main__":
    raise SystemExit(main())