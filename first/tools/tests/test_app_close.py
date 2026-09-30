import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]

def test_gui_close_protocol_exists():
    s = (ROOT / "user" / "window.h").read_text(encoding="utf-8")
    assert "#define GUI_KEY_CLOSE 2" in s

def test_devstudio_escape_requests_window_close():
    s = (ROOT / "user" / "devstudio.c").read_text(encoding="utf-8")
    assert "return GUI_KEY_CLOSE;" in s

def test_editor_close_api_exists():
    s = (ROOT / "user" / "editor.c").read_text(encoding="utf-8")
    assert "void editor_close(void)" in s

def test_wm_handles_gui_key_close():
    s = (ROOT / "user" / "wm.c").read_text(encoding="utf-8")
    assert "consumed == GUI_KEY_CLOSE" in s
    assert "close_app(closing);" in s

def test_language_launcher_opens_devstudio_window():
    s = (ROOT / "user" / "wm.c").read_text(encoding="utf-8")
    start=s.index('if (act == ACT_PYTHON || act == ACT_CC || act == ACT_JAVA)')
    chunk=s[start:start+600]
    assert 'g_app[4].open = 1' in chunk
