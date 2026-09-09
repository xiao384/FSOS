#!/usr/bin/env python3
# 全面校验 PE32+ EFI 应用是否会被 VMware EFI 加载器接受
import sys, struct

def main():
    path = sys.argv[1]
    d = open(path, 'rb').read()
    print("file size:", len(d))
    assert d[0:2] == b'MZ', "no MZ"
    lf = struct.unpack('<I', d[0x3c:0x40])[0]
    print("e_lfanew:", hex(lf))
    assert d[lf:lf+4] == b'PE\x00\x00', "no PE sig"
    # COFF header
    machine = struct.unpack('<H', d[lf+4:lf+6])[0]
    nsec = struct.unpack('<H', d[lf+6:lf+8])[0]
    char = struct.unpack('<H', d[lf+20:lf+22])[0]
    sizeofopt = struct.unpack('<H', d[lf+20:lf+22])[0]
    print("COFF Machine: 0x%04X (%s)" % (machine, "AMD64" if machine==0x8664 else "NOT AMD64!!"))
    print("Characteristics: 0x%04X (DLL=%s, EXEC=%s)" % (char,
          "yes" if char&0x2000 else "no", "yes" if char&0x0002 else "no"))
    opthdr = lf + 24
    magic = struct.unpack('<H', d[opthdr:opthdr+2])[0]
    print("Optional magic: 0x%04X (%s)" % (magic, "PE32+" if magic==0x20b else "NOT PE32+!!"))
    subsys = struct.unpack('<H', d[opthdr+(68 if magic==0x20b else 40):opthdr+(70 if magic==0x20b else 42)])[0]
    print("Subsystem: %d (%s)" % (subsys, "EFI_APPLICATION" if subsys==10 else "NOT EFI!!"))
    imgbase = struct.unpack('<Q', d[opthdr+24:opthdr+32])[0]
    entry = struct.unpack('<I', d[opthdr+16:opthdr+20])[0]
    sectalign = struct.unpack('<I', d[opthdr+32:opthdr+36])[0]
    filealign = struct.unpack('<I', d[opthdr+36:opthdr+40])[0]
    nrvs = struct.unpack('<I', d[opthdr+92:opthdr+96])[0]  # NumberOfRvaAndSizes (PE32+ off 92)
    sizeofimg = struct.unpack('<I', d[opthdr+56:opthdr+60])[0]
    print("ImageBase: 0x%X" % imgbase)
    print("EntryPoint RVA: 0x%X (in image: %s)" % (entry, entry < sizeofimg))
    print("SectionAlignment: 0x%X (%s)" % (sectalign, "pow2" if (sectalign & (sectalign-1))==0 else "NOT POW2!!"))
    print("FileAlignment: 0x%X" % filealign)
    print("NumberOfRvaAndSizes: %d (>=6 needed for BaseReloc: %s)" % (nrvs, "yes" if nrvs>=6 else "NO!!"))
    print("SizeOfImage: 0x%X" % sizeofimg)
    # data dirs
    dd = opthdr + (112 if magic==0x20b else 96)
    print("\nData directories:")
    names = ["Export","Import","Resource","Exception","Security","BaseReloc","Debug","Arch","GlobalPtr","TLS","LoadCfg","BoundImp","IAT","DelayImp","COM","Reserved"]
    for i in range(min(nrvs, 16)):
        rva = struct.unpack('<I', d[dd+i*8:dd+i*8+4])[0]
        sz = struct.unpack('<I', d[dd+i*8+4:dd+i*8+8])[0]
        flag = " <-- BASE RELOC" if i==5 else ""
        if rva or sz:
            print("  [%2d] %-10s RVA=0x%08X size=0x%08X%s" % (i, names[i] if i<len(names) else "?", rva, sz, flag))
    # section table
    print("\nSections (%d):" % nsec)
    st = opthdr + sizeofopt
    for i in range(nsec):
        s = st + i*40
        name = d[s:s+8].rstrip(b'\x00').decode('latin1')
        vsize = struct.unpack('<I', d[s+8:s+12])[0]
        va = struct.unpack('<I', d[s+12:s+16])[0]
        rsize = struct.unpack('<I', d[s+16:s+20])[0]
        raw = struct.unpack('<I', d[s+20:s+24])[0]
        flags = struct.unpack('<I', d[s+36:s+40])[0]
        print("  %-8s VA=0x%08X VSize=0x%08X RawOff=0x%08X RawSize=0x%08X flags=0x%08X" % (
            name, va, vsize, raw, rsize, flags))
    # verify BaseReloc RVA falls inside a section
    br_rva = struct.unpack('<I', d[dd+40:dd+44])[0]
    br_sz = struct.unpack('<I', d[dd+44:dd+48])[0]
    if br_rva:
        inside = any(va <= br_rva < va+vsize for i in range(nsec) for va, vsize in
                     [(struct.unpack('<I', d[st+i*40+12:st+i*40+16])[0],
                       struct.unpack('<I', d[st+i*40+8:st+i*40+12])[0])])
        print("\nBaseReloc RVA 0x%X inside some section: %s" % (br_rva, inside))
        # dump first 16 bytes of reloc block
        # need file offset: find section containing rva
        for i in range(nsec):
            s = st + i*40
            va = struct.unpack('<I', d[s+12:s+16])[0]
            vsize = struct.unpack('<I', d[s+8:s+12])[0]
            raw = struct.unpack('<I', d[s+20:s+24])[0]
            if va <= br_rva < va+vsize:
                fo = raw + (br_rva - va)
                print("  reloc block (file off 0x%X): %s" % (fo, d[fo:fo+16].hex()))

if __name__ == '__main__':
    main()
