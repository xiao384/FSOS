import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class TestGuiCompiler(unittest.TestCase):
    def test_cc_launcher_opens_devstudio(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('ACT_CC', text)
        self.assertIn('devstudio_open_language("C/C++")', text)
        self.assertNotIn('c-demo', text)

    def test_devstudio_run_label_is_compile_or_run(self):
        text = (ROOT / "user" / "devstudio.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('Ctrl+R 编译/运行', text)

if __name__ == '__main__':
    unittest.main()

    def test_python_launcher_opens_devstudio(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        start = text.index('if (act == ACT_PYTHON)')
        chunk = text[start:start+260]
        self.assertIn('devstudio_open_language("Python")', chunk)
        self.assertNotIn('lang_launch("Python", 0, "python-repl")', chunk)

    def test_java_launcher_opens_devstudio(self):
        text = (ROOT / "user" / "wm.c").read_text(encoding="utf-8", errors="replace")
        start = text.index('if (act == ACT_JAVA)')
        chunk = text[start:start+260]
        self.assertIn('devstudio_open_language("Java")', chunk)
        self.assertNotIn('lang_launch("Java",   0, "java-demo")', chunk)

    def test_c_template_has_main(self):
        text = (ROOT / "user" / "devstudio.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('int main()', text)
        self.assertIn('return 0;', text)

    def test_devstudio_has_output_panel_and_mouse(self):
        text = (ROOT / "user" / "devstudio.c").read_text(encoding="utf-8", errors="replace")
        self.assertIn('console_drain', text)
        self.assertIn('int devstudio_on_mouse', text)

    def test_python_runner_returns_to_window(self):
        text = (ROOT / "micropython" / "ports" / "fsos" / "mp_entry.c").read_text(encoding="utf-8", errors="replace")
        self.assertNotIn('Press any key to return to desktop', text)

    def test_devstudio_has_language_launcher_api(self):
        text = (ROOT / "user" / "devstudio.h").read_text(encoding="utf-8", errors="replace")
        self.assertIn('void devstudio_open_language(const char* language);', text)
