#!/usr/bin/env python3
"""test_incremental.py - 增量判定单元测试 (cross_platform 8.1)

断言 mtime + .d 依赖的 SKIP/重编分支正确。
"""
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from kernel_build import IncrementalCache


class TestIncrementalCache(unittest.TestCase):

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.cache = IncrementalCache(self.tmpdir)

    def _write(self, name, content="x"):
        p = os.path.join(self.tmpdir, name)
        with open(p, "w") as fh:
            fh.write(content)
        return p

    def test_obj_missing_needs_compile(self):
        src = self._write("src.c")
        obj = os.path.join(self.tmpdir, "src.o")
        self.assertTrue(self.cache.needs_compile(src, obj))

    def test_obj_fresh_skips(self):
        src = self._write("src.c")
        obj = self._write("src.o")
        time.sleep(0.05)
        os.utime(obj, None)
        self.assertFalse(self.cache.needs_compile(src, obj))

    def test_src_newer_needs_compile(self):
        src = self._write("src.c")
        obj = self._write("src.o")
        time.sleep(0.05)
        os.utime(src, None)
        self.assertTrue(self.cache.needs_compile(src, obj))

    def test_dep_file_newer_needs_compile(self):
        src = self._write("src.c")
        obj = self._write("src.o")
        hdr = self._write("header.h")
        self._write("src.o.d", hdr)
        time.sleep(0.05)
        os.utime(hdr, None)
        self.assertTrue(self.cache.needs_compile(src, obj))

    def test_dep_file_older_skips(self):
        src = self._write("src.c")
        obj = self._write("src.o")
        hdr = self._write("header.h")
        self._write("src.o.d", hdr)
        time.sleep(0.05)
        os.utime(obj, None)
        self.assertFalse(self.cache.needs_compile(src, obj))


if __name__ == "__main__":
    unittest.main()