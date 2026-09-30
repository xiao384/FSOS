import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class TestTerminalAppArchitecture(unittest.TestCase):
    def test_terminal_is_registered_as_wm_app(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('{ "终端"', text)
        self.assertIn('terminal_draw, terminal_key, terminal_on_mouse, terminal_on_tick', text)

    def test_terminal_launch_is_non_modal(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        start = text.index('if (act == ACT_TERMINAL)')
        chunk = text[start:start+420]
        self.assertIn('terminal_open();', chunk)
        self.assertIn('wm_set_focus(ti);', chunk)
        self.assertNotIn('terminal_run();', chunk)

    def test_terminal_header_exposes_window_callbacks(self):
        text = (ROOT / "user" / "terminal.h").read_text(encoding="utf-8", errors="replace")
        for sym in ('terminal_open(void)', 'terminal_draw(int x, int y, int w, int h)',
                    'terminal_key(int k)', 'terminal_on_mouse(int x, int y, int ldown)',
                    'terminal_on_tick(void)', 'terminal_close(void)'):
            self.assertIn(sym, text)

    def test_window_capacity_matches_terminal_slot(self):
        text = (ROOT / "user" / "window.h").read_text(encoding="utf-8", errors="replace")
        self.assertIn('#define MAXAPP 9', text)
        self.assertIn('#define NAPP   9', text)

    def test_terminal_has_native_dynamic_layout(self):
        text = (ROOT / "user" / "terminal.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('#define TERM_COLS        240', text)
        self.assertIn('gfx_font_scale()', text)
        self.assertIn('console_drain', text)

    def test_session_reset_closes_previous_windows(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('static void wm_session_reset(void)', text)
        self.assertIn('if (g_app[i].open) close_app(i);', text)
        self.assertIn('wm_session_reset();', text)

    def test_mouse_cursor_is_large_and_outlined(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('static const uint8_t outer[16]', text)
        self.assertIn('COL_BLACK', text)
        self.assertIn('COL_WHITE', text)

    def test_no_duplicate_mouse_dispatch(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        self.assertNotIn('g_app[hit_idx].on_mouse(mx, my, 1);\\n                            if (g_app[hit_idx].on_mouse)',
                         text)

if __name__ == '__main__':
    unittest.main()
