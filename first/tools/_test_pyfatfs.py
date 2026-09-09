import io
from fatfs.GenFatFS import GenFatFS

size = 64 * 1024 * 1024
fs = GenFatFS.create_filesystem(
    fp=io.BytesIO(b'\x00' * size),
    size=size,
    fat_type='FAT32',
    label='FSOS UEFI',
)
efi = b'MZ' + b'\x00' * 200
with fs.open('EFI/BOOT/BOOTX64.EFI', 'wb') as f:
    f.write(efi)
with fs.open('KERNEL.BIN', 'wb') as f:
    f.write(b'KERNELDATA' * 100)

with fs.open('EFI/BOOT/BOOTX64.EFI', 'rb') as f:
    print('EFI readback len =', len(f.read()))
with fs.open('KERNEL.BIN', 'rb') as f:
    print('KERNEL readback len =', len(f.read()))
print('pyfatfs FAT32 test OK')
