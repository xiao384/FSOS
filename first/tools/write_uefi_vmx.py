#!/usr/bin/env python3
# 生成干净的 ASCII-only UEFI VMX (避免中文注释在 VMware 解析时变成乱码导致 "找不到虚拟机")
import os
out_dir = r"E:\project\clion\project_system\output"
vmx = """\
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "10"
virtualHW.productCompatibility = "hosted"
memsize = "4096"
displayName = "FSOS (UEFI boot)"
guestOS = "other-64"
firmware = "efi"
uefi.secureBoot.enabled = "FALSE"
numvcpus = "1"
cpuid.coresPerSocket = "1"
floppy0.present = "FALSE"
ide0:0.present = "TRUE"
ide0:0.fileName = "FSOS_UEFI.vmdk"
ide0:0.deviceType = "disk"
ide0:0.mode = "persistent"
ide0:0.redo = ""
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "vmware-uefi-serial.log"
serial0.startConnected = "TRUE"
serial0.yieldOnMsrRead = "TRUE"
svga.autodetect = "TRUE"
keyboard.typematic = "TRUE"
msg.autoAnswer = "TRUE"
uuid.action = "keep"
"""
path = os.path.join(out_dir, "FSOS_UEFI.vmx")
with open(path, "w", encoding="ascii") as f:
    f.write(vmx)
print("wrote", path, os.path.getsize(path), "bytes")
