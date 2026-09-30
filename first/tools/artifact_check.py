#!/usr/bin/env python3
"""artifact_check.py - 产物完整性校验模块 (cross_platform 任务 3.6)

为构建/打包尾部提供统一的产物校验: 魔数 (Multiboot2 / OSM1)、体积守卫
(模块 <= 2MB / boot 扇区 512B)、扇区/尺寸自检。校验失败抛 ArtifactError
并保留中间产物用于诊断 (spec 5.1.3 异常 3)。

独立入口: python tools/artifact_check.py --file <path> [--type auto|kernel|module|image]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_config import BuildConfig  # noqa: E402

MAGIC_KERNEL = bytes([0xD6, 0x50, 0x52, 0xE8])
MAGIC_MODULE = b"\x31\x4d\x53\x4f"
EXIT_VERIFY = 2


class ArtifactError(Exception):
    def __init__(self, path, reason):
        super().__init__("[ARTIFACT-FAIL] %s: %s" % (os.path.basename(path), reason))
        self.path = path
        self.reason = reason


class ArtifactReport:
    """收集并报告产物校验结果。"""

    def __init__(self):
        self.items = []

    def add(self, path, ok, detail=""):
        self.items.append((os.path.basename(path), ok, detail))
        return ok

    def summary(self):
        lines = []
        all_ok = True
        for name, ok, detail in self.items:
            all_ok = all_ok and ok
            tag = "OK" if ok else "FAIL"
            lines.append("  [%s] %-20s %s" % (tag, name, detail))
        lines.append("  === %s ===" % ("PASS" if all_ok else "FAIL"))
        return "\n".join(lines), all_ok

    # ------------------------------------------------------------------
    # 单项校验
    # ------------------------------------------------------------------
    @staticmethod
    def verify_kernel_bin(path):
        if not os.path.isfile(path):
            raise ArtifactError(path, "file not found")
        sz = os.path.getsize(path)
        with open(path, "rb") as fh:
            head = fh.read(4)
        if head != MAGIC_KERNEL:
            raise ArtifactError(path, "magic mismatch: %s (expect d65052e8)" % head.hex())
        if sz > BuildConfig.KERNEL_MAX_WARN * 512:
            raise ArtifactError(path, "kernel too large: %d bytes (max %d)" % (sz, BuildConfig.KERNEL_MAX_WARN * 512))
        return True

    @staticmethod
    def verify_module(path):
        if not os.path.isfile(path):
            raise ArtifactError(path, "file not found")
        sz = os.path.getsize(path)
        if sz > BuildConfig.MOD_MAX_BYTES:
            raise ArtifactError(path, "module too large: %d bytes (max %d)" % (sz, BuildConfig.MOD_MAX_BYTES))
        with open(path, "rb") as fh:
            head = fh.read(20)
        if len(head) < 20:
            raise ArtifactError(path, "module header too short: %d bytes" % len(head))
        magic = head[:4]
        if magic != MAGIC_MODULE:
            raise ArtifactError(path, "module magic mismatch: %s (expect 314d534f)" % magic.hex())
        stored_size = struct.unpack_from("<I", head, 12)[0]
        if stored_size != sz:
            raise ArtifactError(path, "size field mismatch: header=%d actual=%d" % (stored_size, sz))
        return True

    @staticmethod
    def verify_boot_sector(path):
        if not os.path.isfile(path):
            raise ArtifactError(path, "file not found")
        sz = os.path.getsize(path)
        if sz != BuildConfig.BOOT_SECTOR_BYTES:
            raise ArtifactError(path, "boot sector size: %d (expect %d)" % (sz, BuildConfig.BOOT_SECTOR_BYTES))
        return True

    @staticmethod
    def verify_image(path, disk_sectors):
        if not os.path.isfile(path):
            raise ArtifactError(path, "file not found")
        sz = os.path.getsize(path)
        expected = disk_sectors * 512
        if sz != expected:
            raise ArtifactError(path, "image size: %d (expect %d)" % (sz, expected))
        with open(path, "rb") as fh:
            boot = fh.read(512)
        if len(boot) < 512:
            raise ArtifactError(path, "cannot read boot sector")
        return True

    # ------------------------------------------------------------------
    # 批量校验 (构建尾部调用)
    # ------------------------------------------------------------------
    def verify_build_outputs(self, output_dir, disk_sectors):
        checks = [
            ("kernel.bin", self.verify_kernel_bin),
            ("kernel_clean.bin", self.verify_kernel_bin),
            ("boot.bin", self.verify_boot_sector),
        ]
        for name, fn in checks:
            p = os.path.join(output_dir, name)
            try:
                fn(p)
                self.add(p, True, "%d bytes" % os.path.getsize(p))
            except ArtifactError as e:
                self.add(p, False, e.reason)
        for mod in ("CINT.MOD", "JVM.MOD"):
            p = os.path.join(output_dir, mod)
            try:
                self.verify_module(p)
                self.add(p, True, "%d bytes" % os.path.getsize(p))
            except ArtifactError as e:
                self.add(p, False, e.reason)
        img = os.path.join(output_dir, "image.img")
        try:
            self.verify_image(img, disk_sectors)
            self.add(img, True, "%d bytes" % os.path.getsize(img))
        except ArtifactError as e:
            self.add(img, False, e.reason)
        _, all_ok = self.summary()
        return all_ok


def main():
    import argparse
    ap = argparse.ArgumentParser(description="FSOS 产物完整性校验")
    ap.add_argument("--file", required=True, help="待校验文件路径")
    ap.add_argument("--type", choices=["auto", "kernel", "module", "image", "boot"],
                    default="auto")
    ap.add_argument("--disk-sectors", type=int, default=8388608)
    args = ap.parse_args()
    t = args.type
    if t == "auto":
        base = os.path.basename(args.file).lower()
        if "kernel" in base and base.endswith(".bin"):
            t = "kernel"
        elif base.endswith(".mod"):
            t = "module"
        elif base == "boot.bin":
            t = "boot"
        else:
            t = "image"
    try:
        if t == "kernel":
            ArtifactReport.verify_kernel_bin(args.file)
        elif t == "module":
            ArtifactReport.verify_module(args.file)
        elif t == "boot":
            ArtifactReport.verify_boot_sector(args.file)
        elif t == "image":
            ArtifactReport.verify_image(args.file, args.disk_sectors)
        print("[OK] %s (%s)" % (args.file, t))
        return 0
    except ArtifactError as e:
        print(str(e), file=sys.stderr)
        return EXIT_VERIFY


if __name__ == "__main__":
    raise SystemExit(main())