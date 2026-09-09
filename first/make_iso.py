#!/usr/bin/env python3
# make_iso.py - 把内核可启动镜像打包成 ISO9660 + El Torito 可启动光盘
#
# 用法: python make_iso.py [--img output/image.img] [--out 输出.iso]
# 默认: --img output/image.img  --out ../iso/FSOS.iso
#
# 引导方式: El Torito 1.44MB 软盘仿真 (media type 2)。
# BIOS 把嵌入的镜像作为虚拟软盘 (dl=0x00), 几何固定 80柱面/2磁头/18扇区 (2880 扇区)。
# 引导链对软盘路径使用 CHS 读 (AH=0x02, AH=0x08 查几何) —— SeaBIOS 的仿真盘
# 不支持 int13h LBA 扩展读 (AH=0x42), 硬盘仿真 (media type 4) 的 CHS 又来自
# MBR 分区表 (本镜像为空), 所以软盘仿真 + CHS 是最可靠的组合。
# media type: 0=noemul, 1=1.2MB, 2=1.44MB, 3=2.88MB, 4=硬盘仿真。
# BIOS 把嵌入的镜像当成虚拟硬盘 (dl=0x80), 引导链的 int13h LBA 读直接可用。
# 镜像内同时放入 kernel.bin / README.md / floppy.img 方便在光盘文件系统里直接取用。

import argparse, os, struct, time

BLK = 2048
CD001 = b"CD001"

# ---------- 基础工具 ----------
def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)

def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)

def pad(sz):
    return (sz + BLK - 1) // BLK * BLK

def iso_date(dt=None):  # 17 字节卷日期
    dt = dt or time.gmtime()
    return bytes([
        dt.tm_year - 1900, dt.tm_mon, dt.tm_mday,
        dt.tm_hour, dt.tm_min, dt.tm_sec, 0,
    ]) + b"\0" * 10

def dir_date(dt=None):  # 7 字节目录日期
    dt = dt or time.gmtime()
    return bytes([
        dt.tm_year - 1900, dt.tm_mon, dt.tm_mday,
        dt.tm_hour, dt.tm_min, dt.tm_sec, 0,
    ])

# ---------- 目录记录 ----------
def dir_record(extent_lba, data_len, flags, name, dt=None):
    name_b = name.encode("ascii") if isinstance(name, str) else name
    rec = bytearray()
    rec.append(0)                          # 占位: 长度, 最后回填
    rec.append(0)                          # 扩展属性长度
    rec += both32(extent_lba)              # 数据区起始 LBA
    rec += both32(data_len)                # 数据长度
    rec += dir_date(dt)                    # 日期
    rec.append(flags)                      # 文件属性
    rec.append(0)                          # 文件单元大小
    rec.append(0)                          # 交错间隔
    rec += both16(1)                       # 卷序列号
    rec.append(len(name_b))                # 文件名长度
    rec += name_b
    if len(rec) % 2 == 0:                  # 偶对齐 (长度已含自身, 需整体偶数)
        rec.append(0)
    rec[0] = len(rec)
    return bytes(rec)

def make_dir(lba, entries):                # entries: (name_bytes, extent_lba, data_len, flags)
    buf = bytearray()
    buf += dir_record(lba, BLK, 0x02, b"\x00")   # "."
    buf += dir_record(lba, BLK, 0x02, b"\x01")   # ".."
    for name, elba, dlen, fl in entries:
        buf += dir_record(elba, dlen, fl, name)
    buf += b"\x00" * (BLK - len(buf))
    return bytes(buf)

def path_table(root_lba):                 # 只含根目录的路径表, 返回 L/M 两种
    # 条目: len_di(1) + ext_attr(1) + extent(4) + parent(2) + id(1=0x00) + 偶对齐
    ent = bytes([1, 0]) + struct.pack("<I", root_lba) + struct.pack("<H", 1) + b"\x00"
    ent += b"\x00" * (len(ent) % 2)
    return ent  # L 表小端; M 表大端替换 extent

# ---------- 卷描述符 ----------
def make_pvd(vol_size, root_lba, root_dir, vol_id="FSOS"):
    b = bytearray(BLK)
    b[0] = 1                                   # 类型: 主卷描述符
    b[1:6] = CD001
    b[6] = 1
    b[8:40]  = b"FSOS".ljust(32, b"\x00")
    b[40:72] = vol_id.ljust(32, "\x00").encode("ascii")
    b[80:88] = both32(vol_size)
    b[120:124] = both16(1)                     # 卷集大小
    b[124:128] = both16(1)                     # 卷序号
    b[128:132] = both16(BLK)                   # 逻辑块大小
    # 路径表大小/位置由调用方回填
    b[156:190] = root_dir                      # 根目录记录
    b[190:318] = b"".ljust(128, b"\x00")
    b[318:446] = b"".ljust(128, b"\x00")
    b[446:574] = b"".ljust(128, b"\x00")
    b[574:702] = b"FSOS".ljust(128, b"\x00")
    b[813:830] = iso_date()
    b[881] = 1                                 # 文件结构版本
    return bytes(b)

def make_boot_record(catalog_lba):
    b = bytearray(BLK)
    b[0] = 0                                   # 引导记录指示符
    b[1:6] = CD001
    b[6] = 1
    b[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    b[71:79] = both32(catalog_lba)             # 引导目录 LBA
    return bytes(b)

def make_terminator():
    b = bytearray(BLK)
    b[0] = 255
    b[1:6] = CD001
    b[6] = 1
    return bytes(b)

def make_boot_catalog(img_lba, media_type=2, load_seg=0x7C0, sect_count=32):
    # Initial/Default Entry 字段偏移 (相对 catalog 扇区):
    #   [32] 引导指示符 0x88  [33] 介质类型  [34:36] 加载段  [36] 系统类型
    #   [38:40] 扇区数 (BIOS 预载到 0x7C00 的 512 字节虚拟扇区数)
    #   [40:44] 引导镜像 LBA
    # 注意: 扇区数是"预载量", 不是整个镜像大小。SeaBIOS 会把它换算成 CD 扇区
    # 读入 0x7C00, 若设为 2880 (整个 1.44MB) 会砸穿中断向量/BDA/BIOS 栈。
    # 预载 32 扇区 (16KB) 覆盖 boot 扇区 + loader (9 扇区), 其余靠 cdemu
    # 把 int13h LBA 读映射到 CD。
    b = bytearray(BLK)
    # 验证条目
    b[0] = 0x01
    b[1] = 0x00                                # 平台: x86
    b[4:28] = b"FSOS Boot".ljust(24, b"\x00")
    b[30] = 0x55
    b[31] = 0xAA
    chk = (0x100 - sum(b[:32])) & 0xFF         # 校验和使总和为 0
    b[28:30] = struct.pack("<H", chk)
    # 初始/默认引导项
    b[32] = 0x88                               # 引导指示符
    b[33] = media_type                         # 2 = 1.44MB 软盘仿真
    b[34:36] = struct.pack("<H", load_seg)
    b[36] = 0                                  # 系统类型
    b[38:40] = struct.pack("<H", sect_count)   # 预载扇区数 (offset 6)
    b[40:44] = struct.pack("<I", img_lba)      # 引导镜像 LBA (offset 8)
    b[64] = 0xAA                               # 结束标志 (可选)
    return bytes(b)

# ---------- 主流程 ----------
def build(floppy_path, extra_files, out_path):
    floppy = open(floppy_path, "rb").read()
    assert len(floppy) == 1474560, f"floppy 镜像需为 1.44MB (实际 {len(floppy)})"
    assert len(floppy) % BLK == 0

    # 磁盘布局
    lba_pvd, lba_br, lba_term = 16, 17, 18
    lba_cat, lba_pt_l, lba_pt_m, lba_root = 19, 20, 21, 22
    cur = 23
    # 引导镜像作为文件 floppy.img 同时放入
    floppy_lba = cur; cur += len(floppy) // BLK
    # 其他文件
    file_extents = []
    for name, data in extra_files:
        file_extents.append((name, cur, len(data)))
        cur += pad(len(data)) // BLK
    total_sectors = cur

    # 构造各块
    pvd = bytearray(make_pvd(total_sectors, lba_root, b"\x00" * 34))
    root_dir = make_dir(lba_root, [
        (b"FLOPPY.IMG", floppy_lba, len(floppy), 0x00),
    ] + [(n.encode("ascii"), e, d, 0x00) for (n, e, d) in file_extents])
    # 回填根目录记录到 PVD
    pvd[156:190] = dir_record(lba_root, BLK, 0x02, b"\x00")

    pt = path_table(lba_root)
    pt_l = bytes([1, 0]) + struct.pack("<I", lba_root) + struct.pack("<H", 1) + b"\x00"
    pt_l += b"\x00" * (len(pt_l) % 2)
    pt_m = bytes([1, 0]) + struct.pack(">I", lba_root) + struct.pack(">H", 1) + b"\x00"
    pt_m += b"\x00" * (len(pt_m) % 2)
    pvd[132:140] = both32(len(pt_l))           # 路径表大小
    pvd[140:144] = struct.pack("<I", lba_pt_l)
    pvd[148:152] = struct.pack("<I", lba_pt_m)

    img = bytearray(total_sectors * BLK)
    def put(lba, data):
        img[lba * BLK:(lba + 1) * BLK] = data.ljust(BLK, b"\x00")
    put(lba_pvd,  bytes(pvd))
    put(lba_br,   make_boot_record(lba_cat))
    put(lba_term, make_terminator())
    put(lba_cat,  make_boot_catalog(floppy_lba))
    put(lba_pt_l, pt_l)
    put(lba_pt_m, pt_m)
    put(lba_root, root_dir)
    put(floppy_lba, floppy)
    for name, e, d in file_extents:
        data = dict(extra_files)[name]
        put(e, data)
    open(out_path, "wb").write(img)
    return total_sectors, out_path

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--img",  default="output/image.img")
    ap.add_argument("--out",  default="../iso/FSOS.iso")
    ap.add_argument("--readme", default="README.md")
    ap.add_argument("--kernel", default="output/kernel.bin")
    a = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    def P(p): return os.path.join(here, p) if not os.path.isabs(p) else p
    img_p = P(a.img)
    if not os.path.exists(img_p):
        print(f"[ISO] 找不到 {img_p}，先运行 build-mingw.ps1 -WithPython"); return 1

    extra = []
    cands = [
        ("KERNEL.BIN", a.kernel),
        ("README.TXT", a.readme),
    ]
    for iso_name, rel in cands:
        p = P(rel)
        if os.path.exists(p):
            extra.append((iso_name, open(p, "rb").read()))
        else:
            print(f"[ISO] 跳过缺失文件 {p}")
    total, out = build(img_p, extra, P(a.out))
    size = os.path.getsize(out)
    print(f"[ISO] {out}")
    print(f"[ISO] {size/1024:.1f} KB, {total} sectors, El Torito 1.44MB floppy emulation")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
