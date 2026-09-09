# ============================================================
# backend.py - 界面后端接口 (核心逻辑与具体 GUI 之间的隔离层)
#
# 由 _upstream/system_os.py + _upstream/ui.py 改造而来:
#   原实现把 tk 对话框直接写在业务逻辑里 (tk.Toplevel / wait_window /
#   messagebox), 导致同一份代码无法脱离 tkinter 运行。这里把所有"显示与
#   交互"收敛成一组后端方法, 于是同一份核心既能:
#     - 跑在 PySide6 桌面窗口 (host/backend_pyside6.py)
#     - 跑在 FSOS 内核的 320x200 文本终端 (kernel/backend_kernel.py)
#     - 跑在纯命令行 (CliBackend, 供无 GUI 环境与自动化测试使用)
#
# 约定: 交互类方法返回 None 一律表示"用户取消"。
#
# 内核兼容: 不使用 f-string / str.format / % 格式化 / 任何第三方或
#           非内置模块 (MicroPython 处于 ROM_LEVEL_MINIMUM)。
# ============================================================


class Backend(object):
    name = 'null'

    # ---- 输出 ----
    def out(self, text):
        print(text)

    def clear(self):
        pass

    def set_title(self, title):
        pass

    # ---- 提示 ----
    def notify(self, title, msg):
        self.out('[i] ' + title + ': ' + msg)

    def warn(self, title, msg):
        self.out('[!] ' + title + ': ' + msg)

    # ---- 交互 ----
    def confirm(self, title, msg, danger=False):
        return False

    def ask_text(self, title, prompt, secret=False):
        return None

    def ask_fields(self, title, fields):
        """fields: [(key, label, secret), ...] 返回 dict 或 None"""
        return None

    def pick(self, title, options):
        """返回选中项的下标 (int) 或 None"""
        return None

    def edit_text(self, title, content):
        """编辑一段文本, 返回新内容或 None (放弃修改)"""
        return None

    # ---- 桌面自动化 (仅宿主机有意义) ----
    def paste(self, text, count, delay):
        return '自动输出在当前后端不可用'

    # ---- 重启终端 ----
    def reboot_app(self):
        return '重启终端在当前后端不可用'


class CliBackend(Backend):
    """纯命令行后端: 无 GUI 环境 / 自动化测试用"""

    name = 'cli'

    def confirm(self, title, msg, danger=False):
        self.out(title + ': ' + msg)
        ans = input('确认? (y/N): ')
        return ans.strip().lower() in ('y', 'yes')

    def ask_text(self, title, prompt, secret=False):
        return input(title + ' - ' + prompt + ': ')

    def ask_fields(self, title, fields):
        self.out('== ' + title + ' ==')
        result = {}
        for item in fields:
            key = item[0]
            label = item[1]
            result[key] = input(label + ': ')
        return result

    def pick(self, title, options):
        self.out('== ' + title + ' ==')
        for i in range(len(options)):
            self.out('  [' + str(i) + '] ' + str(options[i]))
        raw = input('选择序号 (直接回车取消): ').strip()
        if not raw:
            return None
        try:
            idx = int(raw)
        except ValueError:
            return None
        if idx < 0 or idx >= len(options):
            return None
        return idx

    def edit_text(self, title, content):
        self.out('== ' + title + ' ==')
        self.out(content)
        self.out('-- 输入新内容; 独占一行的 "." 结束, 直接 "." 表示放弃 --')
        lines = []
        while True:
            ln = input('')
            if ln == '.':
                break
            lines.append(ln)
        if not lines:
            return None
        return '\n'.join(lines) + '\n'


_backend = None


def set_backend(backend):
    global _backend
    _backend = backend
    return backend


def get_backend():
    global _backend
    if _backend is None:
        _backend = Backend()
    return _backend
