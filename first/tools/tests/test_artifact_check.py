#!/usr/bin/env python3
"""test_artifact_check.py - 产物校验单元测试 (cross_platform 8.1)

断言魔数/体积守卫/扇区检查的正向与负向用例。
"""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from artifact_check import ArtifactReport, ArtifactError, MAGIC_KERNEL, MAGIC_MODULE


class TestArtifactCheck(unittest.TestCase):

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def _write(self, name, data):
        p = os.path.join(self.tmpdir, name)
        with open(p, "wb") as fh:
            fh.write(data)
        return p

    def test_kernel_bin_valid(self):
        p = self._write("kernel.bin", MAGIC_KERNEL + b"\x00" * 100)
        self.assertTrue(ArtifactReport.verify_kernel_bin(p))

    def test_kernel_bin_bad_magic(self):
        p = self._write("kernel.bin", b"\x00\x00\x00\x00" + b"\x00" * 100)
        with self.assertRaises(ArtifactError) as ctx:
            ArtifactReport.verify_kernel_bin(p)
        self.assertIn("magic mismatch", str(ctx.exception))

    def test_kernel_bin_missing(self):
        with self.assertRaises(ArtifactError):
            ArtifactReport.verify_kernel_bin(os.path.join(self.tmpdir, "nope"))

    def test_module_valid(self):
        data = MAGIC_MODULE + b"\x00" * 8 + struct.pack("<I", 256) + b"\x00" * 4
        data = data.ljust(256, b"\x00")
        p = self._write("CINT.MOD", data)
        self.assertTrue(ArtifactReport.verify_module(p))

    def test_module_too_large(self):
        data = MAGIC_MODULE + b"\x00" * (2 * 1024 * 1024 + 100)
        p = self._write("BIG.MOD", data)
        with self.assertRaises(ArtifactError) as ctx:
            ArtifactReport.verify_module(p)
        self.assertIn("too large", str(ctx.exception))

    def test_module_bad_magic(self):
        data = b"\x00\x00\x00\x00" + b"\x00" * 252
        p = self._write("BAD.MOD", data)
        with self.assertRaises(ArtifactError):
            ArtifactReport.verify_module(p)

    def test_boot_sector_valid(self):
        p = self._write("boot.bin", b"\x00" * 512)
        self.assertTrue(ArtifactReport.verify_boot_sector(p))

    def test_boot_sector_wrong_size(self):
        p = self._write("boot.bin", b"\x00" * 511)
        with self.assertRaises(ArtifactError):
            ArtifactReport.verify_boot_sector(p)

    def test_image_valid(self):
        p = self._write("image.img", b"\x00" * (100 * 512))
        self.assertTrue(ArtifactReport.verify_image(p, 100))

    def test_image_wrong_size(self):
        p = self._write("image.img", b"\x00" * (99 * 512))
        with self.assertRaises(ArtifactError):
            ArtifactReport.verify_image(p, 100)


if __name__ == "__main__":
    unittest.main()