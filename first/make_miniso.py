#!/usr/bin/env python3
# make_miniso.py - 构造最小 El Torito No-Emulation ISO 用于隔离测试
# 用法: make_miniso.py <bootimg> <out.iso> [load_segment]
import sys, struct

SECTOR = 2048

def build(bootimg, outiso, load_seg=0x07C0):
    data = open(bootimg, "rb").read()
    # 对齐到 2048 以便 ISO 物理布局一致
    pad = (SECTOR - len(data) % SECTOR) % SECTOR
    data += b"\x00" * pad
    total = 24 + (len(data) + SECTOR - 1) // SECTOR  # 到 boot image 之后
    iso = bytearray(total * SECTOR)

    # PVD @16
    pvd = bytearray(SECTOR)
    pvd[0] = 0x01
    pvd[1:6] = b"CD001"
    pvd[6] = 0x01
    pvd[8:20] = b"FSOS"
    struct.pack_into("<I", pvd, 80, total)
    struct.pack_into("<H", pvd, 120, 1)
    struct.pack_into("<H", pvd, 124, 1)
    struct.pack_into("<H", pvd, 128, 2048)
    struct.pack_into("<I", pvd, 140, 19)   # path table (unused)
    iso[16*SECTOR:17*SECTOR] = pvd

    # Boot Record @17
    br = bytearray(SECTOR)
    br[0] = 0x00
    br[1:6] = b"CD001"
    br[6] = 0x01
    br[7:39] = b"EL TORITO SPECIFICATION"
    struct.pack_into("<I", br, 71, 19)     # catalog LBA
    iso[17*SECTOR:18*SECTOR] = br

    # Terminator @18
    iso[18*SECTOR] = 0xFF
    iso[18*SECTOR+1:18*SECTOR+6] = b"CD001"
    iso[18*SECTOR+6] = 0x01

    # Boot Catalog @19 (2 sectors)
    cat = bytearray(2*SECTOR)
    cat[0] = 0x01
    cat[1] = 0x00
    cat[4:20] = b"MiniBoot"
    cat[30] = 0x55; cat[31] = 0xAA
    s = 0
    for i in range(0, 32, 2):
        s += cat[i] + 256*cat[i+1]
    ck = (0x10000 - (s % 0x10000)) % 0x10000
    cat[28] = ck & 0xFF; cat[29] = (ck>>8)&0xFF
    # Default entry @32
    cat[32] = 0x88
    cat[33] = 0x00
    cat[34] = load_seg & 0xFF; cat[35] = (load_seg>>8)&0xFF
    load_sectors = (len(data) + 511)//512
    cat[38] = load_sectors & 0xFF; cat[39] = (load_sectors>>8)&0xFF
    struct.pack_into("<I", cat, 40, 24)   # load RBA (sector 24, 2048-byte)
    iso[19*SECTOR:21*SECTOR] = cat

    # Boot image @24
    iso[24*SECTOR:24*SECTOR+len(data)] = data

    open(outiso, "wb").write(iso)
    print(f"[mini-iso] bootimg={len(data)}B load_sectors={load_sectors} load_seg=0x{load_seg:X} -> {outiso}")

if __name__ == "__main__":
    boot = sys.argv[1]
    out = sys.argv[2]
    seg = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x07C0
    build(boot, out, seg)
