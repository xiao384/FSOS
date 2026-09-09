#!/usr/bin/env python3
# make_uefi_disk.py - 构造 FSOS 的 UEFI 可启动磁盘镜像
#
# 布局 (必须与 first/boot/uefi/main.c 的常量一致):
#   LBA 0           保护型 MBR (PMBR)
#   LBA 1           GPT 主头
#   LBA 2..33       GPT 分区表 (主)
#   LBA 34..ES        ESP 分区 (FAT32 真实格式), 仅含 \EFI\BOOT\BOOTX64.EFI
#   LBA (34+ESP_SECTORS)       4 字节 LE = 内核扇区数 (供加载器读取)
#   LBA (34+ESP_SECTORS+1)..   内核裸扇区 (来自 kernel_clean.bin)
#   LBA ~DISK-33..             GPT 备份分区表 / 备份头
#
# 说明:
#   * 固定磁盘的 ESP 必须是 FAT32 (UEFI 规范要求; FAT12/16 仅允许可移动介质)。
#     故 ESP 取 64MB, 1 扇区/簇 => 约 13 万簇 (>=65525, 确为 FAT32)。
#   * 目录项首簇: 低 16 位在偏移 26-27, 高 16 位在偏移 20-21。早期版本错把首簇
#     写进高 16 位、低 16 位留 0, 导致真实固件读到 first<<16 而找不到文件 -> "No Media"。
#     此处已修正。
#   * 内核 KERNEL.BIN 现在作为【文件】放在 ESP 根目录, 由 UEFI 加载器按文件名读取,
#     不再是焊死在固定 LBA 的裸扇区 —— 这是"OS 装在硬盘上"而非 Live/PE 镜像的关键。
#   * 内核数据区 (layout.h LBA 3800..4095) 落在 ESP 声明范围内, 但内核用自身 ATA PIO
#     按绝对 LBA 访问, 与文件系统无关。为防 FAT 把这些簇分配给 KERNEL.BIN 造成冲突,
#     构造时把对应簇 (1722..2017) 标为坏簇保留, 分配器跳过它们。
import sys, struct, argparse, os, types, warnings, subprocess

# pyfatfs 在 flush_fat 时会就 "多份 FAT 表不一致" 打印无害 UserWarning (本镜像
# FAT 镜像由库内部处理, 实际只读首份), 但该警告走 stderr, 在调用方
# $ErrorActionPreference='Stop' 下会被 PowerShell 当成 NativeCommandError 中断构建。
# 这里直接抑制, 避免误判。
warnings.filterwarnings("ignore")

# pyfatfs 依赖 fs 包, 而 fs 需要 pkg_resources (本机 setuptools 已不附带)。
# 提供一个最小 shim 即可让 pyfatfs 正常导入。
if 'pkg_resources' not in sys.modules:
    _pr = types.ModuleType('pkg_resources')
    _pr.declare_namespace = lambda name: None
    sys.modules['pkg_resources'] = _pr
from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS

SECTOR = 512
ESP_START = 34
ESP_SECTORS = 67500           # ~33 MB: 1 扇区/簇 -> 66412 数据簇 (>=65525 => 确为 FAT32)
FAT_RESERVED = 32
FAT_NUM = 2
FAT_SECTORS = 1024           # 每 FAT 占用扇区 (容纳 ~13 万簇 * 4 字节)
ROOT_CLUSTER = 2             # FAT32 根目录首簇

ESP_TYPE_GUID = (0xC12A7328, 0xF81F, 0x11D2, bytes([0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B]))

# ---------- 解释器模块 (C/C++ 25 / Java 26 SE) ----------
# 模块 blob 放在 ESP 之后的"未分区空间", 内核运行时用自身 ATA PIO 按 LBA 读取
# (按需载入, 空闲不驻留内存)。以下常量必须与 first/user/module.h / first/layout.h 完全一致。
# 每模块窗口 1GB (2097152 扇区); 两模块共 2GB, 位于 4GB 磁盘高位, 远离 UEFI ESP(≤131105)。
MOD_SECTORS  = 2097152      # 每个模块最多占用扇区 (1GB, 与 layout.h MOD_REGION_SECTORS 一致)
MOD_CINT_LBA = 4000000      # 必须与 layout.h LBA_MOD_CINT 完全一致
MOD_JVM_LBA  = 6097152      # 必须与 layout.h LBA_MOD_JVM 完全一致

# ---------- CRC32 (IEEE 802.3) ----------
def crc32(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF

def guid_bytes(d1, d2, d3, d4):
    return struct.pack('<IHH', d1, d2, d3) + d4

# ---------- FAT32 构造 ----------
# 内核 ATA 持久化区 (layout.h LBA 3800..4095) 按绝对 LBA 由内核直接访问。
# 该区恰好落在 ESP 数据区簇 1702..1997 (几何随 pyfatfs 变化, 需动态计算),
# 若不保留, KERNEL.BIN (3.6MB) 的文件链必然覆盖这些簇 -> 内核写持久化会
# 破坏磁盘上的 KERNEL.BIN, 下次按文件名引导读到损坏内核。故必须在写文件前
# 把这些簇标为坏簇, 让 pyfatfs 分配器跳过 (PyFat.allocate_bytes 会跳过
# BAD_CLUSTER)。
PERSIST_LBA_START = 3800          # 与 layout.h LBA_USER_SB 一致
PERSIST_LBA_END   = 6039          # 内核持久化区 + FS 数据区末尾 (4000+2048-1, 与 FS_DATA_SECS 一致)

def reserve_persistence_clusters(tmp_img):
    """把全局磁盘 LBA 3800..4095 对应的 ESP 数据簇在 FAT 表 (两副本) 中直接
    标为坏簇 (0x0FFFFFF7)。

    必须在 pf.mkfs() + flush_fat() 之后、PyFatFS 打开写文件之前调用: PyFatFS
    打开时从磁盘重新解析 FAT, 读到坏簇后分配器 (allocate_bytes) 会跳过它们,
    KERNEL.BIN 等文件链因而不会占用内核持久化区。

    不依赖 pyfatfs 的内存 fat 视图 (mkfs 后其内部 fat list 是占位态), 直接对
    文件字节打补丁最可靠。
    """
    with open(tmp_img, "rb") as f:
        vol0 = f.read(SECTOR)
    def u16(b,o): return int.from_bytes(b[o:o+2],'little')
    def u32(b,o): return int.from_bytes(b[o:o+4],'little')
    rsvd = u16(vol0, 14)
    numfats = vol0[16]
    fatsz = u32(vol0, 36)          # FATSz32
    spc = vol0[13]
    first_data = rsvd + numfats * fatsz          # 卷内首个数据扇区 (簇2)

    patch = {}
    for lba in range(PERSIST_LBA_START, PERSIST_LBA_END + 1):
        vol_off = lba - ESP_START                 # 卷内扇区号
        if vol_off < first_data:
            continue
        cl = 2 + (vol_off - first_data) // spc    # 簇2 起始于 first_data
        patch[cl] = 1
    if not patch:
        return 0

    with open(tmp_img, "r+b") as f:
        # 对每个 FAT 副本: 起始卷内字节 = (rsvd + i*fatsz) * SECTOR
        for i in range(numfats):
            fat_base = (rsvd + i * fatsz) * SECTOR
            for cl in patch:
                f.seek(fat_base + cl * 4)
                f.write(struct.pack('<I', 0x0FFFFFF7))
    return len(patch)

def build_fat32(efi_data, kernel_data):
    """用真实 FAT 实现 (pyfatfs) 构造合规的 FAT32 ESP, 让 VMware 的 EDK2
    FAT 驱动能够可靠挂载。早期手搓 FAT32 存在若干 EDK2 不接受的边界问题
    (如根目录簇在 FAT 表中未置 EOC), 故改用标准库生成。

    说明:
      * 固定磁盘 ESP 必须是 FAT32 (UEFI 规范; FAT12/16 仅允许可移动介质)。
      * 文件布局: EFI/BOOT/BOOTX64.EFI (UEFI 应用) 与 KERNEL.BIN (内核,
        由 UEFI 加载器按文件名读取) —— OS-on-disk 形态的关键。
      * 内核 ATA 持久化区 (layout.h LBA 3800..4095) 落在 ESP 声明范围内,
        但内核用自身 ATA PIO 按绝对 LBA 访问。为防止 FAT 把持久化簇分配给
        KERNEL.BIN 造成冲突 (内核写持久化 = 破坏磁盘上的内核文件), 构造时
        把对应簇标为坏簇, 分配器跳过它们 (reserve_persistence_clusters)。
    """
    SECTOR = 512
    esp_size = ESP_SECTORS * SECTOR

    here = os.path.dirname(os.path.abspath(__file__))
    tmp = os.path.join(here, "_esp_pyfat.img")
    with open(tmp, "wb") as f:
        f.truncate(esp_size)

    pf = PyFat()
    pf.mkfs(tmp, fat_type=PyFat.FAT_TYPE_FAT32, size=esp_size,
            sector_size=SECTOR, number_of_fats=2, label="FSOS UEFI")
    # pyfatfs 的 mkfs 在分配根目录簇后忘记 flush_fat, 重开时根目录簇在 FAT 中
    # 仍是空闲标记 -> "FREE_CLUSTER mark found in FAT cluster chain". 手动 flush.
    pf.flush_fat()
    # 把内核持久化区对应簇标为坏簇 (直接写 FAT 字节, 两副本), 让分配器跳过。
    nres = reserve_persistence_clusters(tmp)
    print('  persistence LBA %d..%d -> reserved %d clusters as bad (safe for kernel ATA)'
          % (PERSIST_LBA_START, PERSIST_LBA_END, nres))

    fs = PyFatFS(tmp, read_only=False, utc=True)
    fs.makedir("EFI")
    fs.makedir("EFI/BOOT")
    with fs.openbin("EFI/BOOT/BOOTX64.EFI", "wb") as f:
        f.write(bytes(efi_data))
    with fs.openbin("KERNEL.BIN", "wb") as f:
        f.write(bytes(kernel_data))
    fs.close()

    with open(tmp, "rb") as f:
        vol = f.read()
    try:
        os.remove(tmp)
    except OSError:
        pass
    return vol

# ---------- GPT 构造 ----------
def build_gpt(disk_sectors, esp_img, fout):
    # 以稀疏文件流式写入, 避免 4GB bytearray 占用内存 (OOM)。
    # PMBR (保护型 MBR): 分区表项位于偏移 446, 必须含一个 0xEE 类型分区覆盖全盘,
    # 否则 UEFI 固件不识别 GPT -> 报 "No Media" 找不到可启动设备。
    pmbr = bytearray(SECTOR)
    p = 446
    pmbr[p+0] = 0x00                       # 非可启动
    pmbr[p+1:p+4] = bytes([0x00, 0x00, 0x02])  # CHS 起始 (占位)
    pmbr[p+4] = 0xEE                       # 分区类型: 保护型 GPT (关键!)
    pmbr[p+5:p+8] = bytes([0xFE, 0xFF, 0xFF])  # CHS 结束
    struct.pack_into('<I', pmbr, p+8, 1)          # LBA 起始 = 1
    struct.pack_into('<I', pmbr, p+12, disk_sectors - 1)  # LBA 数量 = 全盘
    pmbr[510] = 0x55; pmbr[511] = 0xAA
    fout.seek(0); fout.write(pmbr)

    disk_guid = guid_bytes(0x12345678, 0x9ABC, 0xDEF0, bytes([0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF]))
    part_guid = guid_bytes(0x11223344, 0x5566, 0x7788, bytes([0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF]))

    entries = bytearray(128 * 128)
    e = bytearray(128)
    e[0:16] = guid_bytes(*ESP_TYPE_GUID)
    e[16:32] = part_guid
    struct.pack_into('<Q', e, 32, ESP_START)
    struct.pack_into('<Q', e, 40, ESP_START + ESP_SECTORS - 1)
    struct.pack_into('<Q', e, 48, 0)
    name = 'ESP'.encode('utf-16-le')
    e[56:56 + len(name)] = name
    entries[0:128] = e
    entries_crc = crc32(entries)

    def make_header(current, backup, entries_lba):
        h = bytearray(92)
        h[0:8] = b'EFI PART'
        struct.pack_into('<I', h, 8, 0x00000100)
        struct.pack_into('<I', h, 12, 92)
        struct.pack_into('<I', h, 16, 0)
        struct.pack_into('<I', h, 20, 0)
        struct.pack_into('<Q', h, 24, current)
        struct.pack_into('<Q', h, 32, backup)
        struct.pack_into('<Q', h, 40, 34)                # first usable
        struct.pack_into('<Q', h, 48, disk_sectors - 34)  # last usable
        h[56:72] = disk_guid
        struct.pack_into('<Q', h, 72, entries_lba)
        struct.pack_into('<I', h, 80, 128)
        struct.pack_into('<I', h, 84, 128)
        struct.pack_into('<I', h, 88, entries_crc)
        crc = crc32(h)
        struct.pack_into('<I', h, 16, crc)
        return h

    primary_hdr = make_header(1, disk_sectors - 1, 2)
    backup_hdr = make_header(disk_sectors - 1, 1, disk_sectors - 33)

    fout.seek(1 * SECTOR); fout.write(primary_hdr)
    fout.seek(2 * SECTOR); fout.write(entries)
    fout.seek((disk_sectors - 33) * SECTOR); fout.write(entries)
    # 注意: 按备份头实际长度切片写入 (不可整体切片赋值, 此处为文件对象写)
    bh_off = (disk_sectors - 1) * SECTOR
    fout.seek(bh_off); fout.write(backup_hdr)

    esp_off = ESP_START * SECTOR
    fout.seek(esp_off); fout.write(esp_img)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kernel', required=True, help='kernel_clean.bin')
    ap.add_argument('--efi', required=True, help='BOOTX64.EFI')
    ap.add_argument('--out', required=True, help='output raw disk image')
    ap.add_argument('--cint', default=None, help='CINT.MOD (optional, embedded at MOD_CINT_LBA)')
    ap.add_argument('--jvm', default=None, help='JVM.MOD (optional, embedded at MOD_JVM_LBA)')
    args = ap.parse_args()

    with open(args.efi, 'rb') as f:
        efi_data = f.read()
    with open(args.kernel, 'rb') as f:
        kernel = f.read()

    ksectors = (len(kernel) + SECTOR - 1) // SECTOR
    # 4GB 磁盘: 取 8388608 扇区, 并容纳 PMBR/GPT + ESP + 模块区 (6097152+2097152)
    DISK_SECTORS = max(ESP_START + ESP_SECTORS + 34, MOD_JVM_LBA + MOD_SECTORS + 34, 8388608)
    assert ksectors * SECTOR <= ESP_SECTORS * SECTOR - 4096*1024, 'kernel too big for ESP'

    esp = build_fat32(efi_data, kernel)
    disk_size = DISK_SECTORS * SECTOR
    # 以稀疏文件创建 4GB 磁盘 (截断即分配元数据, 内容按需写入)
    with open(args.out, 'wb') as f:
        f.truncate(disk_size)
    with open(args.out, 'r+b') as f:
        build_gpt(DISK_SECTORS, esp, f)

        # 写入解释器模块 (按需从盘读入预留高地址, 运行完即释放)
        here = os.path.dirname(os.path.abspath(__file__))
        if args.cint and args.jvm:
            # 回填 size / crc32 (若尚未回填)
            try:
                subprocess.run([sys.executable, os.path.join(here, 'patch_mod.py'),
                                args.cint, args.jvm], check=False)
            except Exception as e:
                print('  [warn] patch_mod failed: %s' % e)
            def embed_mod(lba, path):
                if not os.path.exists(path):
                    print('  [warn] module not found, skip:', path); return
                with open(path, 'rb') as mf:
                    data = mf.read()
                pad = (512 - len(data) % 512) % 512
                if pad:
                    data += bytes(pad)
                off = lba * SECTOR
                if off + len(data) > disk_size:
                    raise RuntimeError('module %s overflows disk' % path)
                f.seek(off); f.write(data)
                print('  embedded module at LBA %d: %s (%d bytes)' % (lba, path, len(data)))
            embed_mod(MOD_CINT_LBA, args.cint)
            embed_mod(MOD_JVM_LBA, args.jvm)

    print('UEFI disk image: %s' % args.out)
    print('  size = %d bytes (%d GB)' % (disk_size, disk_size // (1024*1024*1024)))
    print('  ESP  LBA %d..%d (FAT32, %d sectors)' % (ESP_START, ESP_START+ESP_SECTORS-1, ESP_SECTORS))
    print('  MOD  LBA %d (cint) / %d (jvm)' % (MOD_CINT_LBA, MOD_JVM_LBA))
    print('  KERNEL.BIN = %d bytes (file in ESP root, loaded by filename)' % len(kernel))

if __name__ == '__main__':
    main()
