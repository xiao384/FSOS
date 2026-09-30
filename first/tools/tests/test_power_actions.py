import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class TestPowerActions(unittest.TestCase):
    def test_power_action_defined(self):
        s=(ROOT/"user/window.h").read_text(encoding="utf-8")
        self.assertRegex(s, r"#define\s+ACT_POWEROFF\s+\(-7\)")

    def test_startmenu_has_separate_power_action(self):
        s=(ROOT/"user/startmenu.c").read_text(encoding="utf-8")
        self.assertIn("startmenu_footer_hit", s)
        self.assertIn("ACT_POWEROFF", s)
        self.assertIn('"注销"', s); self.assertIn('"关机"', s)

    def test_sidebar_power_action(self):
        s=(ROOT/"user/sidebar.c").read_text(encoding="utf-8")
        self.assertIn("desktop_poweroff", s)
        self.assertIn("电源", s)

    def test_power_service_is_acpi_based_and_does_not_halt_cpu(self):
        s=(ROOT/"kernel/core/power.c").read_text(encoding="utf-8")
        self.assertIn("_S5_", s)
        self.assertIn("poweroff_system", s)
        self.assertNotIn("cli; hlt", s)
        self.assertIn("ACPI_RSDP_ADDR", s)

    def test_wallpaper_header_is_edge_clean(self):
        s=(ROOT/"user/aurora_wallpaper_full.h").read_text(encoding="utf-8")
        self.assertIn("black edge margins removed", s)

    def test_taskbar_has_real_hit_test(self):
        s=(ROOT/"user/taskbar.c").read_text(encoding="utf-8")
        self.assertIn("int taskbar_hit_action", s)

    def test_lfb_path_is_native_and_legacy_gop_no_blackbars(self):
        s=(ROOT/"kernel/drivers/gfx.c").read_text(encoding="utf-8")
        self.assertIn("g_native = 1; g_scale = 1", s)
        self.assertIn("g_gop_disp_w = g_gop_w", s)
        self.assertIn("g_gop_disp_h = g_gop_h", s)

    def test_micropython_power_bridge_uses_power_service(self):
        s=(ROOT/"micropython/ports/fsos/krn_bridge.c").read_text(encoding="utf-8")
        self.assertIn('#include "power.h"', s)
        self.assertIn("poweroff_system", s)
        self.assertNotIn('for (;;) __asm__ volatile("cli; hlt");', s)

if __name__ == "__main__":
    unittest.main()
