from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
terminal = (ROOT / "user" / "terminal.c").read_text(encoding="utf-8")
mouse_h = (ROOT / "kernel" / "drivers" / "mouse.h").read_text(encoding="utf-8")
mouse_c = (ROOT / "kernel" / "drivers" / "mouse.c").read_text(encoding="utf-8")
wm = (ROOT / "user" / "wm.c").read_text(encoding="utf-8")
kb = (ROOT / "kernel" / "drivers" / "kb.h").read_text(encoding="utf-8")

checks = {
    "terminal_scroll_state": "g_scroll_lines" in terminal,
    "terminal_pageup": "KEY_PGUP" in terminal,
    "terminal_pagedown": "KEY_PGDN" in terminal,
    "terminal_wheel": "KEY_WHEEL_UP" in terminal and "KEY_WHEEL_DOWN" in terminal,
    "terminal_scrollbar": "输出区滚动条" in terminal,
    "mouse_wheel_state": "int wheel" in mouse_h,
    "vmmouse_z_wheel": "g_wheel += wz" in mouse_c,
    "wm_routes_wheel": "m.wheel != 0" in wm,
    "wheel_key_codes": "KEY_WHEEL_UP" in kb and "KEY_WHEEL_DOWN" in kb,
}
failed = [k for k,v in checks.items() if not v]
print("terminal scroll regression checks:")
for k,v in checks.items(): print(f"  [{"OK" if v else "FAIL"}] {k}")
if failed:
    raise SystemExit(1)
print("all checks passed")
