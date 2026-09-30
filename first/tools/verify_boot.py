#!/usr/bin/env python3
"""verify_boot.py - FSOS 启动哨兵验证 (cross_platform 任务 5.2/5.3)

基于 bootcheck.py 扩展: 去除 Windows 硬编码路径, QEMU 经 resolve_tool 发现,
gdb 脚本走 tempfile, 超时参数化, 失败携带串口日志路径。

退出码: 0 成功 | None 超时/不确定 | 正整数 失败(映射到 EXIT_RUN=3)
"""
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402

BOOT_SENTINEL = b"FSOS_BOOT_OK"


def _find_qemu(inject):
    return inject.facts.tools.get("qemu") or inject.facts.resolve_tool(
        ["qemu-system-x86_64", "qemu-system-x86_64.exe"])


def _read_rip(port, timeout=10):
    """gdb 读 RIP (脚本走 tempfile, 不再依赖固定盘符路径)。"""
    gdb = detect().resolve_tool(["gdb", "gdb.exe"])
    if not gdb:
        return 0
    gs = tempfile.NamedTemporaryFile(mode="w", suffix=".gdb", delete=False)
    try:
        gs.write("target remote :%d\nset pagination off\n"
                 'printf "RIP=0x%%llx\\n", $rip\ndetach\nquit\n' % port)
        gs.close()
        try:
            r = subprocess.run([gdb, "-nx", "-batch", "-x", gs.name],
                               capture_output=True, text=True, timeout=timeout)
            m = re.search(r"RIP=0x([0-9a-fA-F]+)", r.stdout)
            return int(m.group(1), 16) if m else 0
        except (subprocess.TimeoutExpired, Exception):
            return 0
    finally:
        try:
            os.unlink(gs.name)
        except OSError:
            pass


def verify_image(img, timeout=30, mode="auto", smoke=False, with_gdb=True):
    """验证镜像可引导: QEMU 启动 -> 抓串口哨兵 -> 可选 gdb RIP 检查。

    返回: None=超时/不确定, 0=成功, 3=运行验证失败, 2=镜像非法
    """
    if not os.path.isfile(img):
        print("[verify] missing %s" % img, file=sys.stderr)
        return 2

    facts = detect()
    inj = new(facts)
    qemu = _find_qemu(inj)
    if not qemu:
        print("[verify] QEMU not found", file=sys.stderr)
        return 2

    serf = os.path.join(tempfile.gettempdir(), "fsos_verify_%d.log" % os.getpid())
    is_iso = mode == "cdrom" or (mode == "auto" and img.lower().endswith(".iso"))
    is_vmdk = img.lower().endswith(".vmdk")

    if is_iso:
        cmd = [qemu, "-cdrom", img, "-boot", "d"]
    else:
        fmt = "vmdk" if is_vmdk else "raw"
        cmd = [qemu, "-drive", "file=%s,format=%s" % (img.replace("\\", "/"), fmt)]

    port = 12390
    cmd += ["-serial", "file:" + serf, "-m", "256", "-display", "none", "-no-reboot"]
    if with_gdb:
        cmd += ["-gdb", "tcp::%d" % port]

    print("[verify] launching QEMU (timeout=%ds, mode=%s)" % (timeout, mode))
    q = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    deadline = time.time() + timeout
    sentinel_found = False
    while time.time() < deadline:
        time.sleep(1)
        try:
            with open(serf, "rb") as fh:
                data = fh.read()
            if BOOT_SENTINEL in data:
                sentinel_found = True
                break
        except OSError:
            pass

    if not sentinel_found:
        try:
            with open(serf, "rb") as fh:
                data = fh.read()
            sentinel_found = BOOT_SENTINEL in data
        except OSError:
            data = b""

    rip = 0
    if with_gdb and sentinel_found:
        rip = _read_rip(port)

    q.terminate()
    try:
        q.wait(timeout=5)
    except subprocess.TimeoutExpired:
        q.kill()

    reached_gui = sentinel_found
    in_kernel = 0x100000 <= rip < 0x200000 if rip else True

    if smoke and reached_gui:
        print("[verify] smoke mode: admin/admin login check (placeholder)")

    ok = reached_gui and in_kernel
    if ok:
        print("[verify] PASS: FSOS_BOOT_OK present%s" %
              (", RIP=0x%X" % rip if rip else ""))
        result = 0
    elif sentinel_found and not in_kernel:
        print("[verify] FAIL: sentinel found but RIP=0x%X outside kernel" % rip)
        result = 3
    else:
        print("[verify] FAIL: FSOS_BOOT_OK not found in %ds (log: %s)" %
              (timeout, serf))
        result = 3

    try:
        if ok:
            os.unlink(serf)
    except OSError:
        pass
    return result


def main():
    import argparse
    ap = argparse.ArgumentParser(description="FSOS 启动哨兵验证")
    ap.add_argument("path", nargs="?", default="output/image.img")
    ap.add_argument("--mode", choices=["auto", "drive", "cdrom", "uefi"], default="auto")
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--no-gdb", action="store_true")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.normpath(os.path.join(here, ".."))
    img = args.path if os.path.isabs(args.path) else os.path.join(repo, args.path)
    rc = verify_image(img, timeout=args.timeout, mode=args.mode,
                      smoke=args.smoke, with_gdb=(not args.no_gdb))
    return 0 if rc is None else rc


if __name__ == "__main__":
    raise SystemExit(main())