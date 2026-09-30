#!/usr/bin/env python3
"""test_host_platform.py - 平台注入点驱动表单元测试 (cross_platform 8.1)

断言 Windows/macOS/Linux 三形态的注入点取值与 spec 6.2/design §2.3.2 约定一致。
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from host_platform import detect, new, PlatformFacts, PlatformInjector


class TestPlatformInjection(unittest.TestCase):

    def test_windows_64_injection(self):
        facts = detect(("windows", 64))
        inj = new(facts)
        self.assertEqual(inj.asm_fmt, "win64")
        self.assertEqual(inj.asm_defs, ["-d", "MINGW", "-d", "MINGW64"])
        self.assertEqual(inj.nasm_dbg, ["-g", "-F", "cv8"])
        self.assertEqual(inj.ld_emu, "i386pep")
        self.assertEqual(inj.objcopy_remove, [".reloc", ".bss", ".pdata", ".xdata"])
        self.assertEqual(inj.shell, "cmd")
        self.assertEqual(inj.link_image_base, ["-Wl,--image-base,0"])

    def test_windows_32_injection(self):
        facts = detect(("windows", 32))
        inj = new(facts)
        self.assertEqual(inj.asm_fmt, "win32")
        self.assertEqual(inj.asm_defs, ["-d", "MINGW"])
        self.assertEqual(inj.ld_emu, "i386pe")

    def test_linux_64_injection(self):
        facts = detect(("linux", 64))
        inj = new(facts)
        self.assertEqual(inj.asm_fmt, "elf64")
        self.assertEqual(inj.asm_defs, [])
        self.assertEqual(inj.nasm_dbg, ["-g", "-F", "dwarf"])
        self.assertEqual(inj.ld_emu, "elf_x86_64")
        self.assertEqual(inj.objcopy_remove, [".bss"])
        self.assertEqual(inj.shell, "posix")
        self.assertEqual(inj.link_image_base, [])

    def test_macos_64_injection(self):
        facts = detect(("macos", 64))
        inj = new(facts)
        self.assertEqual(inj.asm_fmt, "elf64")
        self.assertEqual(inj.ld_emu, "elf_x86_64")
        self.assertEqual(inj.shell, "posix")

    def test_unknown_family_posix_fallback(self):
        facts = detect(("freebsd", 64))
        inj = new(facts)
        self.assertEqual(inj.shell, "posix")

    def test_path_sep(self):
        self.assertEqual(PlatformFacts("windows", 64).path_sep, "\\")
        self.assertEqual(PlatformFacts("linux", 64).path_sep, "/")
        self.assertEqual(PlatformFacts("macos", 64).path_sep, "/")

    def test_is_windows(self):
        self.assertTrue(new(detect(("windows", 64))).is_windows())
        self.assertFalse(new(detect(("linux", 64))).is_windows())


if __name__ == "__main__":
    unittest.main()