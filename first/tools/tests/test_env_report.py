#!/usr/bin/env python3
"""test_env_report.py - 补齐命令映射单元测试 (cross_platform 8.1)

断言 winget/brew/apt/dnf/pacman 各形态的安装命令正确生成。
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from env_report import install_hint, _cmp_version, _parse_version


class TestInstallHints(unittest.TestCase):

    def test_windows_nasm(self):
        self.assertIn("winget", install_hint("windows", "nasm"))

    def test_macos_qemu(self):
        self.assertIn("brew", install_hint("macos", "qemu"))

    def test_linux_nasm(self):
        self.assertIn("apt", install_hint("linux", "nasm"))

    def test_linux_cc(self):
        hint = install_hint("linux", "cc")
        self.assertTrue("apt" in hint or "dnf" in hint or "pacman" in hint)

    def test_unknown_family_fallback(self):
        hint = install_hint("freebsd", "nasm")
        self.assertIsInstance(hint, str)


class TestVersionParsing(unittest.TestCase):

    def test_parse_simple(self):
        self.assertEqual(_parse_version("NASM version 2.16.03"), "2.16.03")

    def test_parse_gcc(self):
        self.assertEqual(_parse_version("gcc (GCC) 15.2.0"), "15.2.0")

    def test_parse_none(self):
        self.assertIsNone(_parse_version("no version here"))

    def test_cmp_equal(self):
        self.assertEqual(_cmp_version("2.14", "2.14"), 0)

    def test_cmp_less(self):
        self.assertEqual(_cmp_version("2.13", "2.14"), -1)

    def test_cmp_greater(self):
        self.assertEqual(_cmp_version("2.15", "2.14"), 1)

    def test_cmp_multi_dot(self):
        self.assertEqual(_cmp_version("2.16.03", "2.16"), 1)


if __name__ == "__main__":
    unittest.main()