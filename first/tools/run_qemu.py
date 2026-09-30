#!/usr/bin/env python3
"""run_qemu.py - QEMU 跨平台发现与启动 (cross_platform 任务 5.1)

QEMU 经 resolve_tool (PATH + 候选目录 + 环境变量) 跨平台发现, 支持 -drive 与
-cdrom 形态、无头/图形切换、超时强杀。替代 run-qemu.ps1 语义。
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402


def find_qemu():
    facts = detect()
    return facts.tools.get("qemu") or facts.resolve_tool(
        ["qemu-system-x86_64", "qemu-system-x86_64.exe"])


def run_qemu(img, mode="drive", display="none", timeout=None, extra_args=None):
    """启动 QEMU 运行镜像。

    mode: drive | cdrom
    display: none | graph (图形模式)
    timeout: None=不限, 正整数=秒后强杀
    返回: QEMU 退出码
    """
    qemu = find_qemu()
    if not qemu:
        print("[run] QEMU not found", file=sys.stderr)
        return 4

    if not os.path.isfile(img):
        print("[run] missing %s" % img, file=sys.stderr)
        return 2

    is_vmdk = img.lower().endswith(".vmdk")
    if mode == "cdrom":
        cmd = [qemu, "-cdrom", img, "-boot", "d"]
    else:
        fmt = "vmdk" if is_vmdk else "raw"
        cmd = [qemu, "-drive", "file=%s,format=%s" % (img.replace("\\", "/"), fmt)]

    if display == "none":
        cmd += ["-display", "none"]
    cmd += ["-m", "256", "-no-reboot"]
    if extra_args:
        cmd += list(extra_args)

    print("[run] %s" % " ".join(cmd[:6]))
    p = subprocess.Popen(cmd)

    if timeout is None:
        return p.wait()

    try:
        return p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        p.terminate()
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
        print("[run] QEMU terminated after %ds" % timeout)
        return 0


def main():
    import argparse
    ap = argparse.ArgumentParser(description="FSOS QEMU 运行")
    ap.add_argument("path", nargs="?", default="output/image.img")
    ap.add_argument("--mode", choices=["drive", "cdrom"], default="drive")
    ap.add_argument("--display", choices=["none", "graph"], default="graph")
    ap.add_argument("--timeout", type=int, default=None)
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.normpath(os.path.join(here, ".."))
    img = args.path if os.path.isabs(args.path) else os.path.join(repo, args.path)
    return run_qemu(img, mode=args.mode, display=args.display, timeout=args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())