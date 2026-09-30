import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class TestUIRegressionV23(unittest.TestCase):
    def read(self, rel):
        return (ROOT / rel).read_text(encoding='utf-8', errors='replace')

    def test_mouse_has_native_xy_scaling_and_fullscreen_clamp(self):
        mouse = self.read('kernel/drivers/mouse.c')
        wm = self.read('user/wm.c')
        self.assertIn('int sens = gfx_is_lfb() ? 256 : SENS_FP;', mouse)
        self.assertIn('int ysens = gfx_is_lfb() ? 256 : SENS_FP;', mouse)
        self.assertIn('/ 0xFFFF', mouse)
        self.assertIn('maxy=SCREEN_H-1', wm)
        self.assertIn('static const uint8_t outer[16]', wm)

    def test_language_runtime_is_windowed_and_c_template_is_valid(self):
        mp = self.read('micropython/ports/fsos/mp_entry.c')
        dev = self.read('user/devstudio.c')
        self.assertNotIn('Press any key to return to desktop', mp)
        self.assertNotIn('vga_clear(COL_BLACK);', mp[mp.index('int mp_fsos_run_str'):mp.index('int mp_fsos_run_str')+900])
        self.assertIn('int main()', dev)
        self.assertIn('console_drain', dev)
        self.assertIn('execution timeout', self.read('modules/cint_mod.c'))
        self.assertIn('execution timeout', self.read('modules/jvm_mod.c'))

    def test_relogin_enters_only_modern_wm_session(self):
        app = self.read('user/app.c')
        wm = self.read('user/wm.c')
        self.assertIn('wm_demo_run();', app)
        self.assertIn('g_session = -1;', app)
        self.assertIn('static void wm_session_reset(void)', wm)
        self.assertIn('if (g_app[i].open) close_app(i);', wm)
        self.assertIn('wm_session_reset();', wm)

    def test_wallpaper_covers_full_frame_below_dock(self):
        shell = self.read('user/shell.c')
        self.assertIn('void desktop_draw_wallpaper(void)', shell)
        self.assertIn('int W=VGA_W, H=SCREEN_H;', shell)
        self.assertNotIn('int W=VGA_W, H=WM_TASKBAR_Y;', shell)

    def test_terminal_is_dynamic_and_non_modal(self):
        term = self.read('user/terminal.c')
        wm = self.read('user/wm.c')
        self.assertIn('#define TERM_COLS        240', term)
        self.assertIn('int sc=gfx_font_scale()', term)
        self.assertIn('console_drain', term)
        self.assertIn('terminal_draw, terminal_key, terminal_on_mouse, terminal_on_tick', wm)
        self.assertNotIn('terminal_run();', wm[wm.index('if (act == ACT_TERMINAL)'):wm.index('if (act == ACT_TERMINAL)')+500])

if __name__ == '__main__':
    unittest.main()
