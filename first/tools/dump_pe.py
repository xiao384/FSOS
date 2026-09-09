#!/usr/bin/env python3
import sys, struct
d = open(sys.argv[1], 'rb').read()
print("size", len(d))
print("dos_sig", d[0:2])
lf = struct.unpack('<I', d[0x3c:0x40])[0]
assert d[lf:lf+4] == b'PE\x00\x00', "not PE"
coff = lf + 4
machine, nsec = struct.unpack('<HH', d[coff:coff+4])
ts, psym, nsym = struct.unpack('<III', d[coff+4:coff+16])
soh, chrs = struct.unpack('<HH', d[coff+16:coff+20])
print("machine=0x%X (%s) nsec=%d sizeofopt=%d characteristics=0x%X"
      % (machine, 'x64' if machine==0x8664 else 'OTHER', nsec, soh, chrs))
print("  IMAGE_FILE_DLL(0x2000) set: %s" % bool(chrs & 0x2000))
print("  IMAGE_FILE_EXECUTABLE(0x0002) set: %s" % bool(chrs & 0x0002))
opthdr = coff + 20
magic = struct.unpack('<H', d[opthdr:opthdr+2])[0]
print("opthdr magic=0x%X (%s)" % (magic, 'PE32+' if magic==0x20b else 'PE32' if magic==0x10b else '?'))
if magic == 0x20b:
    sub = struct.unpack('<H', d[opthdr+68:opthdr+70])[0]
    ep = struct.unpack('<I', d[opthdr+16:opthdr+20])[0]   # PE32+ entry at +16
    ib = struct.unpack('<Q', d[opthdr+24:opthdr+32])[0]
    scn = struct.unpack('<I', d[opthdr+56:opthdr+60])[0]  # PE32+ section align at +56
    faf = struct.unpack('<I', d[opthdr+60:opthdr+64])[0]
else:
    sub = struct.unpack('<H', d[opthdr+40:opthdr+42])[0]
    ep = struct.unpack('<I', d[opthdr+16:opthdr+20])[0]
    ib = struct.unpack('<I', d[opthdr+28:opthdr+32])[0]
    scn = struct.unpack('<I', d[opthdr+32:opthdr+36])[0]
    faf = struct.unpack('<I', d[opthdr+36:opthdr+40])[0]
print("subsystem=%d (%s) entry=0x%X imagebase=0x%X sectionAlign=0x%X fileAlign=0x%X"
      % (sub, 'EFI_APP' if sub==10 else 'OTHER', ep, ib, scn, faf))
# sections
shoff = opthdr + soh
for i in range(nsec):
    s = d[shoff + i*40 : shoff + (i+1)*40]
    name = s[0:8]
    vsize, vaddr = struct.unpack('<II', s[8:16])
    rawsize = struct.unpack('<I', s[16:20])[0]   # SizeOfRawData
    rawptr  = struct.unpack('<I', s[20:24])[0]   # PointerToRawData
    chars = struct.unpack('<I', s[36:40])[0]
    print("  sect %d name=%r vaddr=0x%X vsize=0x%X ptrRaw=0x%X rawSize=0x%X chars=0x%X (CODE=%s DATA=%s)"
          % (i, name.rstrip(b'\x00'), vaddr, vsize, rawptr, rawsize, chars,
             bool(chars & 0x20), bool(chars & 0x40)))
