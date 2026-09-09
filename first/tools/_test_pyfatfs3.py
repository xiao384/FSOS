import sys, types, os
if 'pkg_resources' not in sys.modules:
    _pr = types.ModuleType('pkg_resources'); _pr.declare_namespace = lambda n: None
    sys.modules['pkg_resources'] = _pr
from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS

ESP = 64*1024*1024
tmp = "_esp_probe.img"
with open(tmp, "wb") as f: f.truncate(ESP)
pf = PyFat()
pf.mkfs(tmp, fat_type=PyFat.FAT_TYPE_FAT32, size=ESP, sector_size=512,
        number_of_fats=2, label="FSOS UEFI")
pf.flush_fat()
print("pf.fat len =", len(pf.fat))
print("spc =", pf.bpb_header["BPB_SecPerClus"])
print("first_data_sector =", pf.first_data_sector)
cl0 = 2 + (3800 - pf.first_data_sector)//1
cl1 = 2 + (4095 - pf.first_data_sector)//1
# try marking
for cl in range(cl0, cl1+1):
    if cl < len(pf.fat):
        pf.fat[cl] = 0x0FFFFFF7
    else:
        print("OUT OF RANGE cl=", cl)
pf.flush_fat()
# reopen and write, see where KERNEL.BIN lands
fs = PyFatFS(tmp, read_only=False, utc=True)
fs.makedir("EFI"); fs.makedir("EFI/BOOT")
with fs.openbin("EFI/BOOT/BOOTX64.EFI","wb") as f: f.write(b'X'*2048)
with fs.openbin("KERNEL.BIN","wb") as f: f.write(b'K'*200000)
fs.close()
fs2 = PyFatFS(tmp, read_only=True, utc=True)
e = fs2.fs.root_dir.get_entry("KERNEL.BIN")
print("KERNEL.BIN first cluster =", e.get_cluster(), " (bad range", cl0, cl1, ")")
os.remove(tmp)
print("probe done")
