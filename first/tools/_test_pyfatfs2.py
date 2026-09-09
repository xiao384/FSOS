import sys, types, io
# shim pkg_resources so the 'fs' dependency of PyFatFS can import
if 'pkg_resources' not in sys.modules:
    m = types.ModuleType('pkg_resources')
    m.declare_namespace = lambda name: None
    sys.modules['pkg_resources'] = m

from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS, PyFatBytesIOFS

ESP = 64 * 1024 * 1024
tmp = "esp_tmp.img"
efi_bytes = b'MZ' + b'\x00' * 200
kern_bytes = b'KERNELDATA' * 2000

with open(tmp, "wb") as f:
    f.truncate(ESP)
pf = PyFat()
pf.mkfs(tmp, fat_type=PyFat.FAT_TYPE_FAT32, size=ESP,
        sector_size=512, number_of_fats=2, label="FSOS UEFI")
pf.flush_fat()  # pyfatfs mkfs bug: root dir cluster FAT entry not flushed

fs = PyFatFS(tmp, read_only=False, utc=True)
fs.makedir("EFI")
fs.makedir("EFI/BOOT")
with fs.openbin("EFI/BOOT/BOOTX64.EFI", "wb") as f:
    f.write(efi_bytes)
with fs.openbin("KERNEL.BIN", "wb") as f:
    f.write(kern_bytes)
fs.close()

# readback
fs2 = PyFatFS(tmp, read_only=True, utc=True)
with fs2.openbin("EFI/BOOT/BOOTX64.EFI", "rb") as f:
    rb_e = f.read()
with fs2.openbin("KERNEL.BIN", "rb") as f:
    rb_k = f.read()
assert rb_e == efi_bytes, f"EFI mismatch: {len(rb_e)} vs {len(efi_bytes)}"
assert rb_k == kern_bytes, f"KERNEL mismatch: {len(rb_k)} vs {len(kern_bytes)}"
print("OK: FAT32 build + write + readback verified, esp size =", ESP)
