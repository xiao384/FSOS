#!/usr/bin/env python3
# 从 uefi_disk.img 中提取 \EFI\BOOT\BOOTX64.EFI 并做 PE 合法性速检
import sys, struct

SECTOR = 512
ESP_START = 34

def main():
    img = open(sys.argv[1], 'rb').read()
    # GPT primary header at LBA1, entries at LBA2
    # 找 ESP 分区 (type C12A7328...)
    esp_lba = None
    # 简单扫描: 直接假设 ESP 在 LBA 34 (我们构造的就是)
    esp = img[ESP_START*SECTOR:]
    # BPB
    bps = struct.unpack('<H', esp[11:13])[0]
    spc = esp[13]
    reserved = struct.unpack('<H', esp[14:16])[0]
    nfats = esp[16]
    fat32 = struct.unpack('<I', esp[36:40])[0]
    root = struct.unpack('<I', esp[44:48])[0]
    data_start = reserved + nfats*fat32
    print("FAT32: bps=%d spc=%d reserved=%d nfats=%d fatsz=%d rootcl=%d datastart=%d"
          % (bps, spc, reserved, nfats, fat32, root, data_start))

    fat = esp[reserved*SECTOR : (reserved+fat32)*SECTOR]
    def cl2off(cl): return (data_start + (cl-2)*spc)*SECTOR
    def chain(start):
        out = b''
        cl = start
        seen=set()
        while cl and cl < 0x0FFFFFF8 and cl not in seen:
            seen.add(cl)
            out += esp[cl2off(cl):cl2off(cl)+spc*SECTOR]
            cl = struct.unpack('<I', fat[cl*4:cl*4+4])[0] & 0x0FFFFFFF
        return out

    def find_in(dirbuf, name):
        for i in range(0, len(dirbuf), 32):
            e = dirbuf[i:i+32]
            if e[0] in (0, 0xE5): continue
            if e[11] & 0x08: continue  # volume label etc
            if e[0:11] == name:
                first = struct.unpack('<H', e[26:28])[0] | (struct.unpack('<H', e[20:22])[0] << 16)
                size = struct.unpack('<I', e[28:32])[0]
                return first, size
        return None, 0

    # 根目录 (簇 root)
    rootbuf = chain(root)
    # EFI 目录
    fc, fs = find_in(rootbuf, b'EFI        ')
    if fc is None: print("EFI dir NOT found"); return
    ebuf = chain(fc)
    bc, bs = find_in(ebuf, b'BOOT       ')
    if bc is None: print("BOOT dir NOT found"); return
    bbuf = chain(bc)
    xc, xs = find_in(bbuf, b'BOOTX64 EFI')
    if xc is None: print("BOOTX64.EFI NOT found"); return
    data = chain(xc)[:xs]
    open(sys.argv[2], 'wb').write(data)
    print("extracted BOOTX64.EFI: %d bytes -> %s" % (len(data), sys.argv[2]))
    # PE 速检
    assert data[0:2] == b'MZ'
    lf = struct.unpack('<I', data[0x3c:0x40])[0]
    assert data[lf:lf+4] == b'PE\x00\x00'
    machine = struct.unpack('<H', data[lf+4:lf+6])[0]
    opthdr = lf+24
    magic = struct.unpack('<H', data[opthdr:opthdr+2])[0]
    subsys = struct.unpack('<H', data[opthdr+68:opthdr+70])[0]
    nrvs = struct.unpack('<I', data[opthdr+92:opthdr+96])[0]
    char = struct.unpack('<H', data[lf+22:lf+24])[0]
    print("  Machine=0x%X(%s) magic=0x%X subsys=%d nrvs=%d DLL=%s"
          % (machine, "AMD64" if machine==0x8664 else "BAD", magic, subsys, nrvs,
             "yes" if char&0x2000 else "no"))
    if nrvs >= 6:
        br = struct.unpack('<I', data[opthdr+112+40:opthdr+112+44])[0]
        bs = struct.unpack('<I', data[opthdr+112+44:opthdr+112+48])[0]
        print("  BaseReloc RVA=0x%X size=0x%X" % (br, bs))
    else:
        print("  NO BaseReloc (nrvs=%d)" % nrvs)

if __name__ == '__main__':
    main()
