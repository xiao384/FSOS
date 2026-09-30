import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class TestGuiFontContract(unittest.TestCase):
    def test_ui_cjk_is_full_width(self):
        h = (ROOT / "user/cjk.h").read_text(encoding="utf-8")
        self.assertRegex(h, r"#define CJK_UI_CHAR_W\s+16")
        self.assertRegex(h, r"#define CJK_UI_CHAR_H\s+16")

    def test_gui_apps_use_full_cjk_renderer(self):
        for name in ("app.c", "startmenu.c", "taskbar.c", "window.c", "sidebar.c", "shell.c"):
            s = (ROOT / "user" / name).read_text(encoding="utf-8")
            self.assertTrue("cjk_ui_text(" in s or "cjk_text(" in s, msg=name)
            if name in ("shell.c", "startmenu.c", "sidebar.c", "taskbar.c", "window.c"):
                self.assertIn("cjk_ui_text", s, msg=name)


    def test_common_simplified_chinese_glyphs_are_sane(self):
        h = (ROOT / "user/cjk_font.h").read_text(encoding="utf-8")
        cp_match = re.search(r"cjk_codepoints\[CJK_FONT_N\] = \{(.*?)\};", h, re.S)
        self.assertIsNotNone(cp_match)
        cps = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", cp_match.group(1))]
        bm_start = h.index("cjk_bitmaps16[CJK_FONT_N][16]")
        bm_end = h.index("};", bm_start) + 2
        groups = re.findall(r"\{([^{}]+)\}", h[bm_start:bm_end], re.S)
        self.assertEqual(len(cps), len(groups))
        for ch in ("一", "桌", "面", "文", "件", "管", "理", "器", "终", "端", "设", "置"):
            i = cps.index(ord(ch))
            rows = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", groups[i])]
            self.assertEqual(len(rows), 16)
            nonzero = [r for r in rows if r]
            self.assertGreater(len(nonzero), 0, ch)
            # Reject the previous malformed table pattern where glyph rows had
            # unrelated diagonals/noise and the simplest glyphs were not sane.
            self.assertLessEqual(max(bin(r).count("1") for r in rows), 16, ch)
            self.assertGreater(sum(bin(r).count("1") for r in rows), 4, ch)
        yi = cps.index(ord("一"))
        yi_rows = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", groups[yi])]
        self.assertEqual(sum(r != 0 for r in yi_rows), 1)
        self.assertGreaterEqual(bin(yi_rows[next(i for i,r in enumerate(yi_rows) if r)]).count("1"), 8)

    def test_cjk_bitmap_table_exists(self):
        h = (ROOT / "user/cjk_font.h").read_text(encoding="utf-8")
        self.assertIn("cjk_bitmaps16[CJK_FONT_N][16]", h)
        self.assertIn("cjk_lookup(uint32_t cp)", h)

    def test_common_ui_glyphs_24_are_present(self):
        h = (ROOT / "user/cjk_font.h").read_text(encoding="utf-8")
        self.assertIn("cjk_bitmaps24", h)
        self.assertIn("cjk_lookup24(uint32_t cp)", h)
        cp_match = re.search(r"cjk_codepoints\[CJK_FONT_N\] = \{(.*?)\};", h, re.S)
        self.assertIsNotNone(cp_match)
        cps = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", cp_match.group(1))]
        bm_start = h.index("cjk_bitmaps24[CJK_FONT_N][24]")
        bm_end = h.index("};", bm_start) + 2
        groups = re.findall(r"\{([^{}]+)\}", h[bm_start:bm_end], re.S)
        self.assertEqual(len(cps), len(groups))
        for ch in ("桌", "面", "文", "件", "管", "理", "终", "端", "设", "置"):
            i = cps.index(ord(ch))
            rows = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", groups[i])]
            self.assertEqual(len(rows), 24)
            self.assertGreater(sum(bin(r).count("1") for r in rows), 10, ch)

if __name__ == "__main__":
    unittest.main()
