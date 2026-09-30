#!/usr/bin/env python3
"""build_iso.py - 跨平台 El Torito 可启动 ISO 打包 (cross_platform 任务 4.1)

将 pack_iso.ps1 的 No-Emulation 逻辑移植为纯 Python, 同时保留 make_iso.py 的
floppy-emulation 模式 (--mode floppy)。无外部依赖 (不需要 grub-mkrescue/xorriso)。

默认 --mode noemul: iso_boot.bin 经 int13h AH=0x42 从 CD 直接读 kernel.bin。
--mode floppy: El Torito 1.44MB 软盘仿真 (media type 2), 复刻 make_iso.py 语义。

Boot Record Catalog LBA 写入偏移 71 (SeaBIOS/VMware 兼容经验值)。
"""
import argparse
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402
from artifact_check import ArtifactError  # noqa: E402

BLK = 2048
CD001 = b"CD001"
EXIT_ARGS = 5
EXIT_TOOLS = 4
EXIT_BUILD = 1
EXIT_VERIFY = 2


def _ceil_div(a, b):
    return (a + b - 1) // b


def _pad_to(sz, blk):
    return _ceil_div(sz, blk) * blk


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def _dir_record(extent, data_len, flags, name_bytes):
    rec = bytearray()
    rec.append(0)
    rec.append(0)
    rec += both32(extent)
    rec += both32(data_len)
    rec += bytes([126, 1, 1, 0, 0, 0, 0])
    rec.append(flags)
    rec += bytes([0, 0])
    rec += both16(1)
    rec.append(len(name_bytes))
    rec += name_bytes
    if len(rec) % 2 == 0:
        rec.append(0)
    rec[0] = len(rec)
    return bytes(rec)


def _assemble_iso_boot(inject, repo, build_dir, kernel_entry, kernel_lba, kernel_sectors):
    """两遍汇编 iso_boot.asm: 第一遍获取体积, 第二遍注入真实 LBA/扇区数。"""
    iso_boot_asm = os.path.join(repo, "boot", "iso_boot.asm")
    iso_boot_bin = os.path.join(build_dir, "iso_boot.bin")
    argv = (["nasm", "-f", "bin",
             "-d", "KERNEL_ENTRY=0x%X" % kernel_entry,
             "-d", "KERNEL_LBA=%d" % kernel_lba,
             "-d", "KERNEL_SECTORS=%d" % kernel_sectors,
             "-i", repo, "-i", os.path.join(repo, "boot"),
             "-o", iso_boot_bin, iso_boot_asm])
    cp = inject.shell_cmd(argv, capture_output=True)
    if cp.returncode != 0:
        raise RuntimeError("NASM iso_boot.asm failed: %s" %
                           (cp.stderr or b"").decode(errors="replace"))
    with open(iso_boot_bin, "rb") as fh:
        data = fh.read()
    padded_len = _pad_to(len(data), BLK)
    if padded_len > len(data):
        data = data + b"\x00" * (padded_len - len(data))
    with open(iso_boot_bin, "wb") as fh:
        fh.write(data)
    return data


def _resolve_entry(inject, kernel_exe):
    nm = inject.facts.tools.get("nm") or "nm"
    cp = inject.shell_cmd([nm, kernel_exe], capture_output=True)
    for line in (cp.stdout or b"").decode(errors="replace").splitlines():
        p = line.split()
        if len(p) >= 3 and p[1] == "T" and p[2] == "_start":
            return int(p[0], 16)
    raise RuntimeError("Cannot locate _start in %s" % kernel_exe)


# ---------------------------------------------------------------------------
# No-Emulation 模式 (移植自 pack_iso.ps1)
# ---------------------------------------------------------------------------
def build_noemul(repo, output_dir, build_dir, inject, out_path):
    img_path = os.path.join(output_dir, "image.img")
    ker_path = os.path.join(output_dir, "kernel.bin")
    boot_path = os.path.join(output_dir, "boot.bin")
    ker_exe = os.path.join(output_dir, "kernel.exe")
    for p, desc in [(img_path, "image.img"), (ker_path, "kernel.bin"),
                    (boot_path, "boot.bin"), (ker_exe, "kernel.exe")]:
        if not os.path.isfile(p):
            raise ArtifactError(p, "missing %s (run fsos.py build first)" % desc)

    with open(img_path, "rb") as fh:
        img_bytes = fh.read()
    with open(ker_path, "rb") as fh:
        ker_bytes = fh.read()
    with open(boot_path, "rb") as fh:
        boot_bytes = fh.read()

    entry = _resolve_entry(inject, ker_exe)
    print("[NM] kernel entry = 0x%08X" % entry)

    iso_boot = _assemble_iso_boot(inject, repo, build_dir, entry, 24, 1)
    boot_load_sectors = _ceil_div(len(iso_boot), BLK)
    kernel_lba = 24 + boot_load_sectors
    kernel_sectors = _ceil_div(len(ker_bytes), BLK)
    iso_boot = _assemble_iso_boot(inject, repo, build_dir, entry, kernel_lba, kernel_sectors)
    boot_load_sectors = _ceil_div(len(iso_boot), BLK)

    pvd_sec, boot_rec_sec, term_sec = 16, 17, 18
    cat_sec = 19
    pt_l_sec, pt_m_sec, root_dir_sec = 21, 22, 23
    boot_load_sec = 24
    kernel_start_sec = boot_load_sec + boot_load_sectors
    boot_start_sec = kernel_start_sec + kernel_sectors
    boot_sectors = _ceil_div(len(boot_bytes), BLK)
    img_start_sec = boot_start_sec + boot_sectors
    img_sectors = _ceil_div(len(img_bytes), BLK)
    include_img = len(img_bytes) <= 0xFFFFFFFF
    total_sectors = img_start_sec + (img_sectors if include_img else 0)

    iso = bytearray(total_sectors * BLK)

    def put(sec, data):
        off = sec * BLK
        iso[off:off + len(data)] = data

    # PVD
    pvd = bytearray(BLK)
    pvd[0] = 0x01
    pvd[1:6] = CD001
    pvd[6] = 0x01
    pvd[8:12] = b"FSOS"
    pvd[40:48] = b"FSOS_ISO"
    pvd[80:88] = both32(total_sectors)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(BLK)
    pvd[132:140] = both32(10)
    pvd[140:144] = struct.pack("<I", pt_l_sec)
    pvd[148:152] = struct.pack("<I", pt_m_sec)
    root_rec = _dir_record(root_dir_sec, BLK, 0x02, b"\x00")
    pvd[156:156 + len(root_rec)] = root_rec
    pvd[881] = 0x01
    put(pvd_sec, pvd)

    # Boot Record
    br = bytearray(BLK)
    br[0] = 0x00
    br[1:6] = CD001
    br[6] = 0x01
    br[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    struct.pack_into("<I", br, 71, cat_sec)
    put(boot_rec_sec, br)

    # Terminator
    term = bytearray(BLK)
    term[0] = 0xFF
    term[1:6] = CD001
    term[6] = 0x01
    put(term_sec, term)

    # Boot Catalog
    cat = bytearray(2 * BLK)
    cat[0] = 0x01
    cat[4:13] = b"FSOS Boot"
    cat[30] = 0x55
    cat[31] = 0xAA
    s = 0
    for i in range(0, 32, 2):
        s += cat[i] + 256 * cat[i + 1]
    ck = (0x10000 - (s % 0x10000)) % 0x10000
    struct.pack_into("<H", cat, 28, ck)
    cat[32] = 0x88
    cat[33] = 0x00
    struct.pack_into("<H", cat, 34, 0x07C0)
    struct.pack_into("<H", cat, 38, _ceil_div(len(iso_boot), 512))
    struct.pack_into("<I", cat, 40, boot_load_sec)
    put(cat_sec, cat)

    # Path Table
    for pt_sec, endian in ((pt_l_sec, "<"), (pt_m_sec, ">")):
        pt = bytearray(BLK)
        pt[0] = 1
        if endian == "<":
            struct.pack_into("<I", pt, 2, root_dir_sec)
            struct.pack_into("<H", pt, 6, 1)
        else:
            struct.pack_into(">I", pt, 2, root_dir_sec)
            struct.pack_into(">H", pt, 6, 1)
        put(pt_sec, pt)

    # Root Directory
    rd = bytearray(BLK)
    off = 0
    dir_entries = [
        (root_dir_sec, BLK, 0x02, b"."),
        (root_dir_sec, BLK, 0x02, b".."),
        (kernel_start_sec, len(ker_bytes), 0x00, b"KERNEL.BIN;1"),
        (boot_start_sec, len(boot_bytes), 0x00, b"BOOT.BIN;1"),
    ]
    if include_img:
        dir_entries.append((img_start_sec, len(img_bytes), 0x00, b"IMAGE.IMG;1"))
    for extent, dlen, flags, name in dir_entries:
        rec = _dir_record(extent, dlen, flags, name)
        rd[off:off + len(rec)] = rec
        off += len(rec)
    put(root_dir_sec, rd)

    # 文件数据
    put(boot_load_sec, iso_boot)
    put(kernel_start_sec, ker_bytes)
    put(boot_start_sec, boot_bytes)
    if include_img:
        put(img_start_sec, img_bytes)

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(iso)

    _self_check(out_path, pvd_sec, boot_rec_sec, term_sec, cat_sec,
                boot_load_sec, iso_boot, kernel_start_sec, ker_bytes)
    print("[ISO] %s (%d sectors, no-emulation)" % (out_path, total_sectors))
    return 0


def _self_check(iso_path, pvd_sec, boot_rec_sec, term_sec, cat_sec,
                boot_load_sec, iso_boot, kernel_start_sec, ker_bytes):
    with open(iso_path, "rb") as fh:
        bs = fh.read()
    checks = []
    for name, sec, t in [("PVD", pvd_sec, 0x01), ("BootRec", boot_rec_sec, 0x00),
                         ("Term", term_sec, 0xFF)]:
        base = sec * BLK
        ok = bs[base] == t and bs[base + 1:base + 6] == CD001
        checks.append((name, ok))
    bc = cat_sec * BLK
    bc_ok = bs[bc + 30] == 0x55 and bs[bc + 31] == 0xAA and bs[bc + 32] == 0x88 and bs[bc + 33] == 0x00
    checks.append(("BootCatalog", bc_ok))
    s = sum(bs[bc + i] + 256 * bs[bc + i + 1] for i in range(0, 32, 2)) % 0x10000
    checks.append(("Checksum", s == 0))
    br_base = boot_rec_sec * BLK
    br_cat = struct.unpack_from("<I", bs, br_base + 71)[0]
    checks.append(("CatLBA71", br_cat == cat_sec))
    bl_ok = bs[boot_load_sec * BLK:boot_load_sec * BLK + len(iso_boot)] == iso_boot
    checks.append(("BootLoader", bl_ok))
    kbase = kernel_start_sec * BLK
    kern_ok = bs[kbase:kbase + len(ker_bytes)] == ker_bytes
    checks.append(("KernelFile", kern_ok))
    all_ok = all(ok for _, ok in checks)
    for name, ok in checks:
        print("  [CHECK] %-16s %s" % (name, "OK" if ok else "FAIL"))
    if not all_ok:
        raise RuntimeError("ISO self-check FAILED")


# ---------------------------------------------------------------------------
# Floppy 模式 (复刻 make_iso.py)
# ---------------------------------------------------------------------------
def build_floppy(repo, output_dir, out_path):
    import time
    img_path = os.path.join(output_dir, "image.img")
    if not os.path.isfile(img_path):
        raise ArtifactError(img_path, "missing image.img")
    with open(img_path, "rb") as fh:
        floppy = fh.read()
    if len(floppy) != 1474560:
        raise ArtifactError(img_path, "floppy image must be 1.44MB (got %d)" % len(floppy))

    lba_pvd, lba_br, lba_term = 16, 17, 18
    lba_cat, lba_pt_l, lba_pt_m, lba_root = 19, 20, 21, 22
    cur = 23
    floppy_lba = cur
    cur += len(floppy) // BLK
    total_sectors = cur

    def iso_date():
        dt = time.gmtime()
        return bytes([dt.tm_year - 1900, dt.tm_mon, dt.tm_mday,
                      dt.tm_hour, dt.tm_min, dt.tm_sec, 0]) + b"\x00" * 10

    def dir_record(extent_lba, data_len, flags, name):
        name_b = name if isinstance(name, bytes) else name.encode("ascii")
        rec = bytearray()
        rec.append(0)
        rec.append(0)
        rec += both32(extent_lba)
        rec += both32(data_len)
        rec += iso_date()[:7]
        rec.append(flags)
        rec += bytes([0, 0])
        rec += both16(1)
        rec.append(len(name_b))
        rec += name_b
        if len(rec) % 2 == 0:
            rec.append(0)
        rec[0] = len(rec)
        return bytes(rec)

    pvd = bytearray(BLK)
    pvd[0] = 1
    pvd[1:6] = CD001
    pvd[6] = 1
    pvd[8:40] = b"FSOS".ljust(32, b"\x00")
    pvd[40:72] = b"FSOS".ljust(32, b"\x00")
    pvd[80:88] = both32(total_sectors)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(BLK)
    pvd[132:140] = both32(10)
    pvd[140:144] = struct.pack("<I", lba_pt_l)
    pvd[148:152] = struct.pack("<I", lba_pt_m)
    root_rec = dir_record(lba_root, BLK, 0x02, b"\x00")
    pvd[156:156 + len(root_rec)] = root_rec
    pvd[881] = 1

    br = bytearray(BLK)
    br[0] = 0
    br[1:6] = CD001
    br[6] = 1
    br[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    struct.pack_into("<I", br, 71, lba_cat)

    term = bytearray(BLK)
    term[0] = 0xFF
    term[1:6] = CD001
    term[6] = 1

    cat = bytearray(BLK)
    cat[0] = 0x01
    cat[4:13] = b"FSOS Boot"
    cat[30] = 0x55
    cat[31] = 0xAA
    s = sum(cat[i] + 256 * cat[i + 1] for i in range(0, 32, 2))
    struct.pack_into("<H", cat, 28, (0x10000 - (s % 0x10000)) % 0x10000)
    cat[32] = 0x88
    cat[33] = 2
    struct.pack_into("<H", cat, 34, 0x7C0)
    struct.pack_into("<H", cat, 38, 32)
    struct.pack_into("<I", cat, 40, floppy_lba)

    pt_l = bytes([1, 0]) + struct.pack("<I", lba_root) + struct.pack("<H", 1) + b"\x00\x00"
    pt_m = bytes([1, 0]) + struct.pack(">I", lba_root) + struct.pack(">H", 1) + b"\x00\x00"

    rd = bytearray(BLK)
    off = 0
    for rec in [dir_record(lba_root, BLK, 0x02, b"\x00"),
                dir_record(lba_root, BLK, 0x02, b"\x01"),
                dir_record(floppy_lba, len(floppy), 0x00, b"FLOPPY.IMG")]:
        rd[off:off + len(rec)] = rec
        off += len(rec)

    iso = bytearray(total_sectors * BLK)
    iso[lba_pvd * BLK:lba_pvd * BLK + BLK] = pvd
    iso[lba_br * BLK:lba_br * BLK + BLK] = br
    iso[lba_term * BLK:lba_term * BLK + BLK] = term
    iso[lba_cat * BLK:lba_cat * BLK + BLK] = cat
    iso[lba_pt_l * BLK:lba_pt_l * BLK + len(pt_l)] = pt_l
    iso[lba_pt_m * BLK:lba_pt_m * BLK + len(pt_m)] = pt_m
    iso[lba_root * BLK:lba_root * BLK + BLK] = rd
    iso[floppy_lba * BLK:floppy_lba * BLK + len(floppy)] = floppy

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(iso)
    print("[ISO] %s (%d sectors, floppy emulation)" % (out_path, total_sectors))
    return 0


# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------
def pack_main(args):
    facts = detect()
    inj = new(facts)
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.normpath(os.path.join(here, ".."))
    output_dir = os.path.join(repo, "output")
    build_dir = os.path.join(repo, "build-mingw" if facts.family == "windows" else "build")
    os.makedirs(build_dir, exist_ok=True)

    out = args.out if hasattr(args, "out") and args.out else os.path.join(repo, "iso", "FSOS.iso")
    if not os.path.isabs(out):
        out = os.path.join(repo, out)

    mode = args.mode if hasattr(args, "mode") else "noemul"
    try:
        if mode == "floppy":
            return build_floppy(repo, output_dir, out)
        else:
            return build_noemul(repo, output_dir, build_dir, inj, out)
    except ArtifactError as e:
        print(str(e), file=sys.stderr)
        return EXIT_VERIFY
    except RuntimeError as e:
        print("[ISO] %s" % e, file=sys.stderr)
        return EXIT_BUILD


def main():
    ap = argparse.ArgumentParser(description="FSOS 跨平台 ISO 打包")
    ap.add_argument("--mode", choices=["noemul", "floppy"], default="noemul")
    ap.add_argument("--out", default=None)
    return pack_main(ap.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())