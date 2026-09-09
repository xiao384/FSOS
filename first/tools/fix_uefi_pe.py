#!/usr/bin/env python3
# fix_uefi_pe.py - 链接后修正 BOOTX64.EFI 的 PE 头, 使其能被 VMware EFI 加载器接受。
#
# VMware 的 EFI 加载器要求 EFI 应用【必须具备合法的重定位表 (.reloc / BaseReloc)】,
# 否则报 "No compatible bootloader found" 拒绝执行。
#
# MinGW 在 -nostdlib -shared 下生成的 PE 有两个坑:
#   1) NumberOfRvaAndSizes = 0 -> 可选头里【完全没有数据目录表】, 即使存在 .reloc 节,
#      BaseReloc 数据目录项也不存在 -> 加载器判定"无重定位表"而拒绝。
#   2) 用 -fno-pic 链接时连 .reloc 节都不会生成 (自包含单对象被链接器在期内解析)。
#
# 本脚本统一处理:
#   - 强制 Subsystem=10, 清空 import 目录, 校验和=0, 置位 DLL(0x2000)+EXEC(0x0002)。
#   - 设 NumberOfRvaAndSizes=16, 可选头扩到 112+16*8=240, 把节表+所有节原始数据整体
#     后移 128 字节并修正各 PointerToRawData, 消除可选头与节表重叠。
#   - 若镜像缺 .reloc 节, 则在末尾【注入】一个最小合法的 .reloc 节
#     (含一条 IMAGE_REL_BASED_ABSOLUTE=0 的占位块, 加载器安全忽略)。
#   - 把 BaseReloc 数据目录(索引5)指向 .reloc 节, 让 VMware 找到合法重定位表。
import sys, struct

def align(x, a):
    return (x + a - 1) & ~(a - 1)

def main():
    if len(sys.argv) < 3:
        print("usage: fix_uefi_pe.py <in.efi> <out.efi>")
        sys.exit(1)
    inp, outp = sys.argv[1], sys.argv[2]
    d = bytearray(open(inp, 'rb').read())
    lf = struct.unpack('<I', d[0x3c:0x40])[0]
    assert d[lf:lf+4] == b'PE\x00\x00', "not a PE"
    opthdr = lf + 24
    magic = struct.unpack('<H', d[opthdr:opthdr+2])[0]
    assert magic == 0x20b, "only PE32+ supported"
    soh = struct.unpack('<H', d[lf+20:lf+22])[0]   # COFF: SizeOfOptionalHeader (at lf+20, after the 4-byte PE sig + 20-byte COFF header)

    # ---- 1. 强制 Subsystem = 10 (EFI_APPLICATION) ----
    struct.pack_into('<H', d, opthdr+68, 10)

    nrvs = 16   # 数据目录数量 (需 >=6 以容纳 BaseReloc 索引5)

    # ---- 2. 清空整个数据目录数组, 仅保留 BaseReloc (EFI 应用不需要 export/import/exception 等) ----
    dd0 = opthdr + 112
    for i in range(nrvs):
        struct.pack_into('<I', d, dd0 + i*8, 0)
        struct.pack_into('<I', d, dd0 + i*8 + 4, 0)

    # ---- 3. 计算并设置合法 PE 校验和 (VMware 文件型 EFI 加载器会校验) ----
    csum_off = opthdr + 64
    struct.pack_into('<I', d, csum_off, 0)
    if len(d) % 2:
        d.append(0)
    s = 0
    for i in range(0, len(d), 2):
        if i == csum_off:
            continue
        w = d[i] | (d[i+1] << 8)
        s += w
        if s > 0xFFFF:
            s = (s & 0xFFFF) + (s >> 16)
    s = (s & 0xFFFF) + (s >> 16)
    csum = (s + len(d)) & 0xFFFFFFFF
    struct.pack_into('<I', d, csum_off, csum)

    # ---- 4. 置为干净的 EFI 应用特征位: DLL(0x2000) + EXECUTABLE_IMAGE(0x0002) + LARGE_ADDRESS_AWARE(0x0020) ----
    char = 0x2000 | 0x0002 | 0x0020
    struct.pack_into('<H', d, lf+22, char)

    # ---- 5. 确保数据目录存在: NumberOfRvaAndSizes 已在步骤2前设为16, 此处扩可选头并平移节表 ----
    new_soh = 112 + nrvs * 8          # 240 for PE32+
    shift = new_soh - soh
    nsec = struct.unpack('<H', d[lf+6:lf+8])[0]
    if shift > 0:
        d = d[:opthdr+soh] + bytearray(shift) + d[opthdr+soh:]
        st = opthdr + new_soh
        for i in range(nsec):
            s = st + i*40
            raw = struct.unpack('<I', d[s+20:s+24])[0]
            if raw:
                struct.pack_into('<I', d, s+20, raw + shift)
        struct.pack_into('<H', d, lf+16, new_soh)
    struct.pack_into('<I', d, opthdr+92, nrvs)

    st = opthdr + new_soh
    filealign = struct.unpack('<I', d[opthdr+36:opthdr+40])[0]
    sectalign = struct.unpack('<I', d[opthdr+32:opthdr+36])[0]
    sizeofimage = struct.unpack('<I', d[opthdr+56:opthdr+60])[0]

    # ---- 6. 找 .reloc 节; 没有就注入 ----
    reloc_va = None
    reloc_vsize = None
    for i in range(nsec):
        s = st + i*40
        if d[s:s+8].startswith(b'.reloc'):
            reloc_va = struct.unpack('<I', d[s+12:s+16])[0]
            reloc_vsize = struct.unpack('<I', d[s+8:s+12])[0]
            break

    if reloc_va is None:
        # 注入最小合法 .reloc 节: 一个重定位块, 含两条 TYPE=0(ABSOLUTE) 占位项(4 字节对齐)
        new_va = align(sizeofimage, sectalign)
        new_raw = align(len(d), filealign)
        reloc_block = (struct.pack('<I', new_va) +        # PageRVA
                       struct.pack('<I', 12) +             # BlockSize (4 对齐)
                       struct.pack('<H', 0x0000) +         # 占位项1: type0, off0
                       struct.pack('<H', 0x0000))          # 占位项2: type0, off0
        reloc_vsize = len(reloc_block)
        reloc_raw = reloc_block + b'\x00' * (align(reloc_vsize, filealign) - reloc_vsize)
        sh = bytearray(40)
        sh[0:8] = b'.reloc\x00\x00'
        struct.pack_into('<I', sh, 8, reloc_vsize)      # VirtualSize
        struct.pack_into('<I', sh, 12, new_va)          # VirtualAddress
        struct.pack_into('<I', sh, 16, len(reloc_raw))  # SizeOfRawData
        struct.pack_into('<I', sh, 20, new_raw)         # PointerToRawData
        struct.pack_into('<I', sh, 36, 0x42000040)      # INITIALIZED_DATA | READ
        # 节头插到节表末尾
        d[st + nsec*40 : st + nsec*40 + 40] = sh
        # 文件末尾填补到 new_raw 并追加节数据
        d.extend(b'\x00' * (new_raw - len(d)))
        d.extend(reloc_raw)
        reloc_va = new_va
        # 更新节数 / SizeOfImage / SizeOfHeaders
        struct.pack_into('<H', d, lf+6, nsec + 1)
        struct.pack_into('<I', d, opthdr+56, new_va + align(reloc_vsize, sectalign))
        nsec += 1

    # ---- 7. BaseReloc 数据目录(索引5) 指向 .reloc 节 ----
    struct.pack_into('<I', d, dd0+40, reloc_va)
    struct.pack_into('<I', d, dd0+44, reloc_vsize)

    new_soh_end = opthdr + new_soh + nsec*40
    struct.pack_into('<I', d, opthdr+60, align(new_soh_end, filealign))

    open(outp, 'wb').write(d)
    print("fixed PE: subsystem=10, DLL flag set, NumberOfRvaAndSizes=%d, header grown by %d" % (nrvs, shift))
    print("  BaseReloc -> RVA=0x%X size=0x%X (%s .reloc) -> %s" % (
        reloc_va, reloc_vsize, "reused" if reloc_va is not None else "injected", outp))

if __name__ == '__main__':
    main()
