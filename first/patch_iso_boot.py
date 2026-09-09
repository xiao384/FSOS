import os, subprocess, math, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(SCRIPT_DIR, "build-mingw")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")
ISO_DIR = os.path.join(os.path.dirname(SCRIPT_DIR), "iso")

os.makedirs(BUILD_DIR, exist_ok=True)

# 1. 读 kernel 入口
nm_out = subprocess.check_output(["nm", os.path.join(OUTPUT_DIR, "kernel.exe")], text=True)
entry = None
for line in nm_out.splitlines():
    if line.endswith(" T _start"):
        entry = int(line.split()[0], 16)
        break
if entry is None:
    sys.exit("Cannot find _start")
print(f"[NM] kernel entry = 0x{entry:08X}")

# 2. 计算 KERNEL_LBA / KERNEL_SECT（与 pack_iso.ps1 一致）
kernel_bin = os.path.join(OUTPUT_DIR, "kernel.bin")
kernel_size = os.path.getsize(kernel_bin)
kernel_sectors = math.ceil(kernel_size / 2048)
iso_boot_load_sec = 24
kernel_lba = iso_boot_load_sec + 2  # iso_boot 补齐为 2 个 ISO 扇区
print(f"[ISO] kernel @ LBA {kernel_lba}, {kernel_sectors} sectors")

# 3. 重新汇编 iso_boot.bin
iso_boot_bin = os.path.join(BUILD_DIR, "iso_boot.bin")
subprocess.check_call([
    "nasm", "-f", "bin",
    f"-d", f"KERNEL_ENTRY=0x{entry:X}",
    f"-d", f"KERNEL_LBA={kernel_lba}",
    f"-d", f"KERNEL_SECT={kernel_sectors}",
    "-i", SCRIPT_DIR,
    "-i", BUILD_DIR,
    "-o", iso_boot_bin,
    os.path.join(SCRIPT_DIR, "iso_boot.asm")
])

# 4. 补齐到 2048 字节整数倍（2 个 ISO 扇区）
with open(iso_boot_bin, "rb") as f:
    iso_boot = f.read()
pad_len = math.ceil(len(iso_boot) / 2048) * 2048
if pad_len > len(iso_boot):
    iso_boot += b"\x00" * (pad_len - len(iso_boot))
print(f"[BOOT] iso_boot.bin {len(iso_boot)} bytes ({pad_len//2048} ISO sectors)")
with open(iso_boot_bin, "wb") as f:
    f.write(iso_boot)

# 5. 直接打补丁替换 ISO 中的引导镜像
iso_path = os.path.join(ISO_DIR, "FSOS.iso")
with open(iso_path, "r+b") as f:
    f.seek(iso_boot_load_sec * 2048)
    f.write(iso_boot)
print(f"[PATCH] patched {len(iso_boot)} bytes @ ISO sector {iso_boot_load_sec}")
print(f"[DONE] {iso_path}")
