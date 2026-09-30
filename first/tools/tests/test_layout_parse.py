#!/usr/bin/env python3
"""test_layout_parse.py - 布局解析单元测试 (cross_platform 8.1)

断言 layout.h 各 LBA/扇区常量正确解析, 重叠校验通过。
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from layout_parse import parse_layout_text, load_layout, LayoutMap, LayoutError


SAMPLE_LAYOUT = """
#define DISK_SECTORS      8388608
#define LBA_BOOT          0
#define LBA_LOADER        1
#define LBA_KERNEL        9
#define LBA_USER_SB       6000
#define LBA_USER_REC      6001
#define LBA_FS_DIR        6040
#define LBA_FS_DATA       6060
#define LBA_SYSCONF       6020
#define LBA_MOD_CINT      4000000
#define LBA_MOD_JVM       6097152
#define MOD_REGION_SECTORS 2097152
"""


class TestLayoutParse(unittest.TestCase):

    def test_parse_sample(self):
        lm = parse_layout_text(SAMPLE_LAYOUT)
        self.assertEqual(lm.DISK_SECTORS, 8388608)
        self.assertEqual(lm.LBA_BOOT, 0)
        self.assertEqual(lm.LBA_LOADER, 1)
        self.assertEqual(lm.LBA_KERNEL, 9)
        self.assertEqual(lm.LBA_MOD_CINT, 4000000)
        self.assertEqual(lm.LBA_MOD_JVM, 6097152)
        self.assertEqual(lm.MOD_REGION_SECTORS, 2097152)

    def test_dict_access(self):
        lm = parse_layout_text(SAMPLE_LAYOUT)
        self.assertEqual(lm["DISK_SECTORS"], 8388608)
        self.assertEqual(lm.get("LBA_BOOT"), 0)
        self.assertIsNone(lm.get("NONEXISTENT"))

    def test_missing_required_raises(self):
        with self.assertRaises(LayoutError):
            LayoutMap({"DISK_SECTORS": 100})

    def test_real_layout_h(self):
        lm = load_layout()
        self.assertEqual(lm.LBA_MOD_CINT, 4000000)
        self.assertEqual(lm.LBA_MOD_JVM, 6097152)
        self.assertGreater(lm.DISK_SECTORS, 0)

    def test_overlap_check_passes(self):
        lm = parse_layout_text(SAMPLE_LAYOUT)
        lm.seal_kernel_end(1410)
        self.assertTrue(lm.validate_overlap())

    def test_overlap_detects_collision(self):
        text = SAMPLE_LAYOUT.replace("#define LBA_KERNEL        9",
                                     "#define LBA_KERNEL        5990")
        lm = parse_layout_text(text)
        lm.seal_kernel_end(6010)
        with self.assertRaises(LayoutError):
            lm.validate_overlap()

    def test_define_with_comment(self):
        text = "#define DISK_SECTORS 8388608 // total sectors\n"
        lm = parse_layout_text(text + SAMPLE_LAYOUT.split("\n", 1)[1])
        self.assertEqual(lm.DISK_SECTORS, 8388608)


if __name__ == "__main__":
    unittest.main()