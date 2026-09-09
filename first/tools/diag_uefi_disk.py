#!/usr/bin/env python3
# diag_uefi_disk.py - 逐层诊断 UEFI 磁盘镜像 (PMBR/GPT/FAT32/目录), 找出 VMware 拒绝的真实原因
import sys, struct
SECTOR = 512
def rd(img, lba, n=1):
    return img[lba*SECTOR:(lba+n)*SECTOR]

def crc32(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF

def main():
    img = open(sys.argv[1], 'rb').read()
    N = len(img)//SECTOR
    print("=== disk: %d sectors (%d bytes) ===" % (N, len(img)))

    # 1) PMBR
    pmbr = rd(img, 0)
    print("[PMBR] sig=%s boot=0x%02X ptype=0x%02X" % (pmbr[510:512].hex(), pmbr[0], pmbr[446+4]))
    assert pmbr[510:512] == b'\x55\xaa', "PMBR no 0x55AA"
    assert pmbr[446+4] == 0xEE, "PMBR protective partition not 0xEE"

    # 2) GPT header @ LBA1
    gh = rd(img, 1)
    assert gh[0:8] == b'EFI PART', "GPT header signature missing"
    rev = struct.unpack('<I', gh[8:12])[0]
    hsize = struct.unpack('<I', gh[12:16])[0]
    stored_crc = struct.unpack('<I', gh[16:20])[0]
    hcopy = bytearray(gh[:hsize]); hcopy[16:20] = b'\x00\x00\x00\x00'
    calc_crc = crc32(bytes(hcopy))
    currentLBA = struct.unpack('<Q', gh[24:32])[0]
    backupLBA  = struct.unpack('<Q', gh[32:40])[0]
    firstUsable= struct.unpack('<Q', gh[40:48])[0]
    lastUsable = struct.unpack('<Q', gh[48:56])[0]
    entriesLBA = struct.unpack('<Q', gh[72:80])[0]
    nEntries   = struct.unpack('<I', gh[80:84])[0]
    entrySize  = struct.unpack('<I', gh[84:88])[0]
    entriesCRC = struct.unpack('<I', gh[88:92])[0]
    print("[GPT] rev=0x%X hsize=%d currentLBA=%d backupLBA=%d firstUsable=%d lastUsable=%d entriesLBA=%d nEntries=%d entrySize=%d entriesCRC=0x%X"
          % (rev, hsize, currentLBA, backupLBA, firstUsable, lastUsable, entriesLBA, nEntries, entrySize, entriesCRC))
    print("       header CRC stored=0x%08X calc=0x%08X -> %s" % (stored_crc, calc_crc, "OK" if stored_crc==calc_crc else "MISMATCH"))

    # 3) partition entries
    elba = struct.unpack('<Q', gh[72:80])[0]
    ecnt = struct.unpack('<I', gh[80:84])[0]
    esz  = struct.unpack('<I', gh[84:88])[0]
    entblob = rd(img, elba, (ecnt*esz+SECTOR-1)//SECTOR)
    ecalc = crc32(entblob[:ecnt*esz])
    estored = struct.unpack('<I', gh[88:92])[0]
    print("[GPT] entries CRC stored=0x%08X calc=0x%08X -> %s" % (estored, ecalc, "OK" if estored==ecalc else "MISMATCH"))
    ESP = (0xC12A7328,0xF81F,0x11D2,bytes([0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B]))
    for i in range(min(ecnt,4)):
        e = entblob[i*esz:(i+1)*esz]
        tg = struct.unpack('<IHH', e[0:8]); tg = (tg[0], tg[1], tg[2], e[8:16])
        first = struct.unpack('<Q', e[32:40])[0]
        last = struct.unpack('<Q', e[40:48])[0]
        is_esp = (tg[0]==ESP[0] and tg[1]==ESP[1] and tg[2]==ESP[2] and tg[3]==ESP[3])
        name = e[56:120].decode('utf-16-le', 'ignore').rstrip('\x00')
        print("  part%d: type=%s first=%d last=%d name='%s'%s" % (i, 'ESP' if is_esp else 'other', first, last, name, '  <== ESP' if is_esp else ''))
        if is_esp:
            esp_first, esp_last = first, last

    esp_size = esp_last - esp_first + 1
    print("=== ESP partition: LBA %d..%d (%d sectors) ===" % (esp_first, esp_last, esp_size))

    # 4) FAT32 BPB
    bpb = rd(img, esp_first)
    print("[FAT32] jump=%s oem=%s bps=%d spc=%d reserved=%d nfats=%d fatsz32=%d total32=%d rootclus=%d"
          % (bpb[0:3].hex(), bpb[3:11], struct.unpack('<H',bpb[11:13])[0], bpb[13],
             struct.unpack('<H',bpb[14:16])[0], bpb[16], struct.unpack('<I',bpb[36:40])[0],
             struct.unpack('<I',bpb[32:36])[0], struct.unpack('<I',bpb[44:48])[0]))
    bps = struct.unpack('<H',bpb[11:13])[0]
    spc = bpb[13]
    reserved = struct.unpack('<H',bpb[14:16])[0]
    nfats = bpb[16]
    fatsz = struct.unpack('<I',bpb[36:40])[0]
    total = struct.unpack('<I',bpb[32:36])[0]
    data_start = reserved + nfats*fatsz
    clusters = (total - data_start)//spc
    print("        data_start=%d clusters=%d -> FAT type: %s" % (data_start, clusters, 'FAT32' if clusters>=65525 else 'FAT16/12(!)'))
    last_cluster_sector = data_start + (clusters-1)*spc
    print("        last cluster data sector=%d  volume total-1=%d  -> %s" % (last_cluster_sector, total-1, 'OK' if last_cluster_sector<=total-1 else 'OVERFLOW (cluster exceeds volume!)'))
    print("        boot sig @510=%s" % bpb[510:512].hex())

    # 5) walk directory to find \EFI\BOOT\BOOTX64.EFI
    fat_off = esp_first + reserved
    fat = {}
    for s in range(fatsz):
        chunk = rd(img, fat_off+s, 1)
        for i in range(128):
            fat[reserved*0 + s*128 + i] = struct.unpack('<I', chunk[i*4:i*4+4])[0]
    def cl2sec(cl): return esp_first + data_start + (cl-2)*spc
    def read_chain(start):
        out=bytearray(); cl=start
        while cl and cl<0x0FFFFFF8:
            out += rd(img, cl2sec(cl), spc)
            nxt = fat.get(cl,0)&0x0FFFFFFF
            if nxt==cl: break
            cl=nxt
        return bytes(out)
    def name11(e):
        n=e[0:11]
        if n[0]==0: return None
        if n[0]==0xE5: return '<free>'
        if e[11]&0x08: return n.decode('ascii', 'replace')
        s = n[0:8].decode('ascii','replace').rstrip() + '.' + n[8:11].decode('ascii','replace').rstrip()
        return s.rstrip('.')
    def dir_list(cl):
        d=read_chain(cl); entries=[]
        for i in range(0,len(d),32):
            e=d[i:i+32]
            nm=name11(e)
            if nm is None: continue
            low=int.from_bytes(e[26:28],'little'); high=int.from_bytes(e[20:22],'little')
            fcl=(high<<16)|low
            entries.append((nm, e[11], fcl, int.from_bytes(e[28:32],'little')))
        return entries
    root = dir_list(2)
    print("[DIR] root: %s" % ", ".join("%s(0x%X,c%d)"%(n,a,c) for n,a,c,s in root))
    efi = [e for e in root if e[0].upper()=='EFI']
    assert efi, "no EFI dir in root"
    bootdir = dir_list(efi[0][2])
    print("[DIR] EFI: %s" % ", ".join("%s(c%d)"%(n,c) for n,a,c,s in bootdir))
    boot = [e for e in bootdir if e[0].upper()=='BOOT']
    assert boot, "no BOOT dir in EFI"
    bx = dir_list(boot[0][2])
    print("[DIR] BOOT: %s" % ", ".join("%s(c%d,s%d)"%(n,c,s) for n,a,c,s in bx))
    bx64 = [e for e in bx if e[0].upper()=='BOOTX64.EFI']
    if bx64:
        data = read_chain(bx64[0][2])
        print("[OK] BOOTX64.EFI found: %d bytes, starts with %s" % (len(data), data[:4].hex()))
    else:
        print("[FAIL] BOOTX64.EFI NOT FOUND")
    kx = [e for e in root if e[0].upper().startswith('KERNEL')]
    if kx:
        data = read_chain(kx[0][2])
        print("[OK] %s found: %d bytes, magic %s" % (kx[0][0], len(data), data[:4].hex()))

if __name__ == '__main__':
    main()
