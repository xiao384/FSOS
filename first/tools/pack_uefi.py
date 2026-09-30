#!/usr/bin/env python3
"""pack_uefi.py - UEFI 端到端打包 (cross_platform 任务 4.2)

将 make_uefi_vm_auto.ps1 拆分为 Python 步骤:
  1. 编译 BOOTX64.EFI (跨平台一致标志 -fno-pic -mno-red-zone 等)
  2. 复用 make_uefi_disk.py 生成 GPT+FAT32(ESP) 磁盘
  3. qemu-img convert 转 vmdk (经 resolve_tool 发现)
  4. VMX 模板生成 (firmware=efi)
Linux 下跳过 VMware 子步骤 (vmdk/VMX) 仅提示 (spec 5.2.1 规则 3)。
"""
import os
import struct
import subprocess
import sys
import shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402

EXIT_BUILD = 1
EXIT_TOOLS = 4
EXIT_VERIFY = 2


def _resolve_entry(inject, kernel_exe):
    nm = inject.facts.tools.get("nm") or "nm"
    cp = inject.shell_cmd([nm, kernel_exe], capture_output=True)
    for line in (cp.stdout or b"").decode(errors="replace").splitlines():
        p = line.split()
        if len(p) >= 3 and p[1] == "T" and p[2] == "_start":
            v = int(p[0], 16)
            if v > 0x100000:
                return v
    return 0x100020


def _compile_uefi_efi(inject, repo, output_dir, build_dir):
    """编译 BOOTX64.EFI (MinGW PE32+ EFI 应用, subsystem 10)。"""
    entry = _resolve_entry(inject, os.path.join(output_dir, "kernel.exe"))
    print("[UEFI] kernel entry = 0x%X" % entry)
    uefi_main_c = os.path.join(repo, "boot", "uefi", "main.c")
    uefi_main_o = os.path.join(build_dir, "uefi_main.o")
    efi_path = os.path.join(output_dir, "BOOTX64.EFI")

    cc = inject.facts.tools.get("cc") or "gcc"
    cflags = ["-c", "-fno-pic", "-fno-pie", "-ffreestanding", "-fno-stack-protector",
              "-mgeneral-regs-only", "-mno-red-zone", "-m64",
              "-DKERNEL_ENTRY=0x%XULL" % entry,
              "-I", os.path.join(repo, "boot", "uefi"),
              "-o", uefi_main_o, uefi_main_c]
    cp = inject.shell_cmd([cc] + cflags, capture_output=True)
    if cp.returncode != 0:
        raise RuntimeError("compile uefi main.c failed: %s" %
                           (cp.stderr or b"").decode(errors="replace"))

    lflags = ["-nostdlib", "-shared", "-Wl,--subsystem,10", "-Wl,-e,efi_main",
              "-Wl,--image-base,0x400000", "-o", efi_path, uefi_main_o]
    cxx = inject.facts.tools.get("cxx") or "g++"
    cp = inject.shell_cmd([cxx] + lflags, capture_output=True)
    if cp.returncode != 0:
        raise RuntimeError("link BOOTX64.EFI failed: %s" %
                           (cp.stderr or b"").decode(errors="replace"))

    fix_pe = os.path.join(os.path.dirname(__file__), "fix_uefi_pe.py")
    if os.path.isfile(fix_pe):
        subprocess.run([sys.executable, fix_pe, efi_path, efi_path],
                       capture_output=True)
    print("[UEFI] BOOTX64.EFI %d bytes" % os.path.getsize(efi_path))
    return efi_path


def _make_uefi_disk(repo, output_dir, efi_path):
    """复用 make_uefi_disk.py 生成 GPT+FAT32(ESP) 磁盘，并嵌入模块。"""
    kclean = os.path.join(output_dir, "kernel_clean.bin")
    if not os.path.isfile(kclean):
        raise RuntimeError("kernel_clean.bin not found; run fsos.py build first")

    # 防止出现“源码已经有 sidebar，但 VMware 仍启动旧内核”的假绿灯。
    # 只有当 sidebar.c 存在时才强制要求链接产物导出 sidebar_draw。
    kexe = os.path.join(output_dir, "kernel.exe")
    sidebar_src = os.path.join(repo, "user", "sidebar.c")
    if os.path.isfile(sidebar_src) and os.path.isfile(kexe):
        nm = shutil.which("nm") or shutil.which("llvm-nm") or "nm"
        try:
            cp_nm = subprocess.run([nm, kexe], capture_output=True, text=True)
            if " sidebar_draw" not in (cp_nm.stdout or ""):
                raise RuntimeError("kernel.exe does not contain sidebar_draw; rebuild kernel before packaging VMware")
        except FileNotFoundError:
            print("[UEFI] warning: nm unavailable; cannot verify sidebar_draw")

    disk_img = os.path.join(output_dir, "uefi_disk.img")
    make_disk = os.path.join(os.path.dirname(__file__), "make_uefi_disk.py")
    cmd = [sys.executable, make_disk, "--kernel", kclean, "--efi", efi_path, "--out", disk_img]
    cint = os.path.join(output_dir, "CINT.MOD")
    jvm = os.path.join(output_dir, "JVM.MOD")
    if os.path.isfile(cint):
        cmd += ["--cint", cint]
    if os.path.isfile(jvm):
        cmd += ["--jvm", jvm]
    cp = subprocess.run(cmd, capture_output=True, text=True)
    if cp.returncode != 0:
        raise RuntimeError("make_uefi_disk.py failed: %s" % cp.stderr)
    shutil.copyfile(kclean, os.path.join(output_dir, "KERNEL.BIN"))
    print("[UEFI] uefi_disk.img %d bytes" % os.path.getsize(disk_img))
    return disk_img


_VMX_TEMPLATE = """\
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "10"
virtualHW.productCompatibility = "hosted"
memsize = "4096"
displayName = "FSOS (UEFI boot)"
guestOS = "other-64"
firmware = "efi"
numvcpus = "2"
cpuid.coresPerSocket = "2"
floppy0.present = "FALSE"
ide0:0.present = "TRUE"
ide0:0.fileName = "FSOS_UEFI.vmdk"
ide0:0.deviceType = "disk"
ide0:0.mode = "persistent"
ide0:0.startConnected = "TRUE"
bios.bootOrder = "ide0:0"
efi.quickBoot.enabled = "FALSE"
ethernet0.present = "FALSE"
usb.present = "FALSE"
sound.present = "FALSE"
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "vmware-uefi-serial.log"
serial0.yieldOnMsrRead = "FALSE"
RemoteDisplay.vnc.enabled = "TRUE"
RemoteDisplay.vnc.port = "5901"
RemoteDisplay.vnc.auth = "none"
"""


def _convert_vmdk(inject, disk_img, out_root):
    """qemu-img convert 转 vmdk (经 resolve_tool 发现)。"""
    qemu_img = inject.facts.resolve_tool(
        ["qemu-img", "qemu-img.exe"],
        candidates=[r"C:\Program Files\qemu"] if inject.is_windows() else None)
    if not qemu_img:
        raise RuntimeError("qemu-img not found")
    vmdk = os.path.join(out_root, "FSOS_UEFI.vmdk")
    cp = inject.shell_cmd([qemu_img, "convert", "-f", "raw", "-O", "vmdk",
                           disk_img, vmdk], capture_output=True)
    if cp.returncode != 0:
        raise RuntimeError("qemu-img convert failed: %s" %
                           (cp.stderr or b"").decode(errors="replace"))
    print("[UEFI] vmdk %d KB" % (os.path.getsize(vmdk) // 1024))
    return vmdk


def _write_vmx(out_root):
    vmx = os.path.join(out_root, "FSOS_UEFI.vmx")
    with open(vmx, "w", encoding="utf-8") as fh:
        fh.write(_VMX_TEMPLATE)
    print("[UEFI] VMX %s" % vmx)
    return vmx


def pack_uefi_main(args):
    facts = detect()
    inj = new(facts)
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.normpath(os.path.join(here, ".."))
    output_dir = os.path.join(repo, "output")
    build_dir = os.path.join(output_dir, "uefi_build")
    os.makedirs(build_dir, exist_ok=True)

    try:
        efi = _compile_uefi_efi(inj, repo, output_dir, build_dir)
        disk_img = _make_uefi_disk(repo, output_dir, efi)

        if facts.family == "linux":
            print("[UEFI] Linux: skipping VMware (vmdk/VMX) steps")
            print("[UEFI] uefi_disk.img ready for QEMU OVMF boot")
            return 0

        out_root = os.path.join(repo, "output", "Auto")
        os.makedirs(out_root, exist_ok=True)
        _convert_vmdk(inj, disk_img, out_root)
        _write_vmx(out_root)
        print("[UEFI] done: %s" % out_root)
        return 0
    except RuntimeError as e:
        print("[UEFI] %s" % e, file=sys.stderr)
        return EXIT_BUILD


def main():
    import argparse
    ap = argparse.ArgumentParser(description="FSOS UEFI 端到端打包")
    return pack_uefi_main(ap.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())