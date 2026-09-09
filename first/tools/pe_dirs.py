#!/usr/bin/env python3
import sys, struct
d = open(sys.argv[1], 'rb').read()
lf = struct.unpack('<I', d[0x3c:0x40])[0]
coff = lf + 4
soh = struct.unpack('<H', d[coff+16:coff+18])[0]
opthdr = coff + 20
magic = struct.unpack('<H', d[opthdr:opthdr+2])[0]
# PE32+ data directory starts at opthdr+112, each 8 bytes (RVA, size), 16 entries
dd_off = opthdr + (112 if magic==0x20b else 96)
names = ['Export','Import','Resource','Exception','Security','BaseReloc','Debug','Arch','GlobalPtr','TLS','LoadCfg','BoundImp','IAT','DelayImp','CLR','Reserved']
print("Data directories (%d entries):" % (soh and (soh- (112 if magic==0x20b else 96))//8))
for i in range(16):
    rva, size = struct.unpack('<II', d[dd_off+i*8:dd_off+i*8+8])
    if rva or size:
        print("  [%2d] %-10s RVA=0x%X size=0x%X" % (i, names[i], rva, size))
print("NumberOfRvaAndSizes field =", struct.unpack('<I', d[opthdr+(108 if magic==0x20b else 92):opthdr+(108 if magic==0x20b else 92)+4])[0])
