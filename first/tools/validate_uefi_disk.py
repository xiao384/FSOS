#!/usr/bin/env python3
# 校验 uefi_disk.img 结构正确性 (无法无头启动, 用结构检查代替)
# 重点: FAT32 目录项首簇 = 低16位@26-27 + 高16位@20-21。
#   校验完全基于 BPB 动态几何 (reserved / numfats / fatsz / spc / rootclus),
#   不写死 FAT 大小等常量 —— ESP 由 pyfatfs 生成, 几何随实现变化。
# 额外检查: 内核持久化区 (layout.h LBA 3800..4095, 绝对磁盘 LBA) 对应簇必须
#   在 FAT 中标为坏簇 (0x0FFFFFF7), 防止 KERNEL.BIN 文件链占用后内核写持久化
#   破坏磁盘上的内核文件。
import sys, struct
SECTOR = 512
ESP_START = 34                 # 磁盘 LBA 34 起为 ESP 卷 (与 make_uefi_disk.py 一致)
ESP_SECTORS = 131072
ESP_TYPE = bytes([0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B])
PERSIST_LBA_START = 3800
PERSIST_LBA_END   = 4095
BAD_CLUSTER = 0x0FFFFFF7

def main():
    p = sys.argv[1]
    d = open(p,'rb').read()
    ok = True
    def chk(cond, msg):
        nonlocal ok
        print(('PASS' if cond else 'FAIL') + ' - ' + msg)
        if not cond: ok = False

    # 1) PMBR
    chk(d[510]==0x55 and d[511]==0xAA, 'PMBR 0x55AA at LBA0')
    # 2) GPT header
    g = d[SECTOR:SECTOR+92]
    chk(g[0:8]==b'EFI PART', 'GPT header signature at LBA1')
    # 3) partition entry
    e = d[2*SECTOR:2*SECTOR+128]
    chk(e[0:16]==ESP_TYPE, 'partition 0 is ESP type GUID')
    first = int.from_bytes(e[32:40],'little'); last = int.from_bytes(e[40:48],'little')
    chk(first==ESP_START and last==ESP_START+ESP_SECTORS-1, 'ESP LBA range %d..%d'%(first,last))

    # ESP 卷
    esp = d[ESP_START*SECTOR:(ESP_START+ESP_SECTORS)*SECTOR]
    chk(esp[510]==0x55 and esp[511]==0xAA, 'ESP boot sector 0x55AA')

    # ---- 动态读取 BPB 几何 (FAT32) ----
    def u16(off): return int.from_bytes(esp[off:off+2],'little')
    def u32(off): return int.from_bytes(esp[off:off+4],'little')
    bps  = u16(11)
    spc  = esp[13]
    rsvd = u16(14)
    numfats = esp[16]
    fatsz = u32(36)                     # FATSz32
    rootclus = u32(44)
    chk(bps==SECTOR, 'BPB bytes/sector = %d'%bps)
    chk(rsvd>=1 and numfats>=1 and fatsz>0, 'BPB rsvd=%d nfats=%d fatsz=%d'%(rsvd,numfats,fatsz))
    data_start = rsvd + numfats * fatsz           # 首个数据扇区 (簇2)
    fat_off = rsvd * bps

    # FAT[0] / FAT[1] 标记
    def fat_entry(cl):
        off = fat_off + cl*4
        return int.from_bytes(esp[off:off+4],'little') & 0x0FFFFFFF
    chk(fat_entry(0)==0x0FFFFFF8, 'FAT[0] media marker (got 0x%X)'%fat_entry(0))
    chk(fat_entry(1)==0x0FFFFFFF, 'FAT[1] EOC (got 0x%X)'%fat_entry(1))

    def cluster_sector(cl): return data_start + (cl - 2) * spc
    def read_cluster(cl):
        s = cluster_sector(cl)
        return esp[s*bps:(s+spc)*bps]
    def read_chain(first_cl):
        out = b''
        cl = first_cl
        guard = 0
        while cl and cl < 0x0FFFFFF8 and guard < 200000:
            out += read_cluster(cl)
            nxt = fat_entry(cl)
            if nxt >= 0x0FFFFFF8: break
            cl = nxt; guard += 1
        return out

    def dir_first_cluster(buf, name11):
        for i in range(0, len(buf), 32):
            if buf[i:i+11]==name11:
                low  = int.from_bytes(buf[i+26:i+28],'little')
                high = int.from_bytes(buf[i+20:i+22],'little')
                return (high<<16)|low
        return None

    # 根目录 (RootClus) -> EFI
    root = read_chain(rootclus)
    chk(len(root)>=32, 'root dir chain readable (%d bytes)'%len(root))
    chk(dir_first_cluster(root, b'EFI        ') is not None, 'root has EFI dir')
    efi_dir = read_chain(dir_first_cluster(root, b'EFI        '))
    chk(dir_first_cluster(efi_dir, b'BOOT       ') is not None, 'EFI has BOOT dir')
    boot_dir = read_chain(dir_first_cluster(efi_dir, b'BOOT       '))
    chk(dir_first_cluster(boot_dir, b'BOOTX64 EFI') is not None, 'BOOT has BOOTX64.EFI')
    bx = read_chain(dir_first_cluster(boot_dir, b'BOOTX64 EFI'))
    chk(bx[0:2]==b'MZ', 'BOOTX64.EFI starts with PE/MZ signature')

    # KERNEL.BIN 作为 ESP 根目录下的文件存在 (文件式 OS 引导)
    chk(dir_first_cluster(root, b'KERNEL  BIN') is not None, 'ESP root has KERNEL.BIN')
    kx = read_chain(dir_first_cluster(root, b'KERNEL  BIN'))
    chk(len(kx) > 4096, 'KERNEL.BIN size = %d bytes'%len(kx))
    chk(kx[0:4]==b'\xd6\x50\x52\xe8', 'KERNEL.BIN starts with Multiboot2 magic')

    # 内核持久化簇 (磁盘 LBA 3800..4095 -> ESP 数据簇) 必须被标为坏簇, 与文件链不重叠
    #   LBA 转卷内扇区 = lba - ESP_START; 簇号 = 2 + (扇区 - data_start)/spc
    miss = 0
    first_bad = True
    for lba in range(PERSIST_LBA_START, PERSIST_LBA_END + 1):
        vol_off = lba - ESP_START
        if vol_off < data_start:
            miss += 1
            continue
        cl = 2 + (vol_off - data_start)//spc
        if fat_entry(cl) != BAD_CLUSTER:
            miss += 1
            if first_bad:
                print('FAIL - persistence LBA %d -> cluster %d NOT bad (got 0x%X)'
                      % (lba, cl, fat_entry(cl)))
                first_bad = False
    chk(miss==0, 'persistence LBA %d..%d clusters all reserved bad' %
        (PERSIST_LBA_START, PERSIST_LBA_END))

    print('RESULT: %s' % ('ALL PASS' if ok else 'HAS FAILURES'))
    sys.exit(0 if ok else 1)

if __name__=='__main__':
    main()
