# ============================================================
# ui_pyside6.py - PySide6 图形界面 (对应 _upstream/ui.py)
#
# 与原 tkinter 版的差异:
#   - 单窗口: 登录成功后不再销毁重建根窗口, 而是关闭登录窗、打开终端窗,
#     避免 Qt 下反复构造 QApplication 带来的隐患 (对应原 tk 版"第二次
#     Tk() 窗口可能不显示"的坑);
#   - 增加命令历史 (上下键) 与提示符着色, 原版只有单行输入无历史;
#   - 所有对话框经 host/backend_pyside6.py, 业务命令本身在 core/ 里,
#     本文件只负责界面。
# ============================================================
import json
import os
import platform
import sys

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QTextCursor
from PySide6.QtWidgets import (QApplication, QFormLayout, QLineEdit,
                               QMainWindow, QMessageBox, QPushButton,
                               QTextEdit, QVBoxLayout, QWidget)

from core.backend import set_backend
from core.commands import run_cmd_text
from core.config import config
from core.ptos import SYSTEM_OS
from host import theme
from host.backend_pyside6 import PySide6Backend

APP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USERS_FILE = os.path.join(APP_DIR, 'users_data.json')

# 当前已打开的终端窗口引用, 供 apply_theme_now() 在 theme 命令后被调用刷新皮肤
_CURRENT_WINDOW = None


def load_users():
    with open(USERS_FILE, 'r', encoding='utf-8') as f:
        return json.load(f)


class _CommandLine(QLineEdit):
    """带上下键历史的命令输入框"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.history = []
        self.hist_pos = 0
        self.draft = ''

    def push(self, line):
        if line and (not self.history or self.history[-1] != line):
            self.history.append(line)
        self.hist_pos = len(self.history)

    def keyPressEvent(self, event):
        key = event.key()
        if key == Qt.Key.Key_Up:
            if self.history:
                if self.hist_pos == len(self.history):
                    self.draft = self.text()
                self.hist_pos = max(0, self.hist_pos - 1)
                self.setText(self.history[self.hist_pos])
            return
        if key == Qt.Key.Key_Down:
            if self.history:
                self.hist_pos = min(len(self.history), self.hist_pos + 1)
                if self.hist_pos == len(self.history):
                    self.setText(self.draft)
                else:
                    self.setText(self.history[self.hist_pos])
            return
        super().keyPressEvent(event)


class LoginWindow(QWidget):
    def __init__(self, on_success):
        super().__init__()
        self.on_success = on_success
        self.setWindowTitle('Better terminal - 登录')
        self.resize(320, 200)
        layout = QFormLayout(self)
        self.name_edit = QLineEdit(self)
        self.key_edit = QLineEdit(self)
        self.key_edit.setEchoMode(QLineEdit.EchoMode.Password)
        layout.addRow('用户名:', self.name_edit)
        layout.addRow('密码:', self.key_edit)
        button = QPushButton('登录 (Enter)', self)
        button.clicked.connect(self.try_login)
        layout.addRow(button)
        self.name_edit.returnPressed.connect(self.try_login)
        self.key_edit.returnPressed.connect(self.try_login)

    def try_login(self):
        try:
            data = load_users()
        except Exception as e:
            QMessageBox.critical(self, '错误', '无法读取用户数据: %s' % e)
            return
        name = self.name_edit.text()
        pwd = self.key_edit.text()
        role = None
        for i in range(len(data['users_name'])):
            if data['users_name'][i] == name and data['users_password'][i] == pwd:
                role = data['users_root'][i]
                break
        if role is None:
            QMessageBox.warning(self, '错误', '用户名或密码错误')
            return
        self.on_success(name, role)


class TerminalWindow(QMainWindow):
    def __init__(self, user, role):
        super().__init__()
        self.setWindowTitle('bash-%s%s--%s--%s' %
                            (platform.system(), platform.release(), user, role))
        self.resize(760, 600)

        central = QWidget(self)
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(6)

        self.output = QTextEdit(central)
        self.output.setReadOnly(True)
        self.output.setFont(QFont('Consolas', 10))
        layout.addWidget(self.output, 1)

        self.cmdline = _CommandLine(central)
        self.cmdline.setFont(QFont('Consolas', 10))
        self.cmdline.returnPressed.connect(self.submit)
        layout.addWidget(self.cmdline, 0)

        colors = theme.get_theme()
        self.output.setTextColor(colors['fg'])
        self.setStyleSheet(theme.stylesheet(colors))

        # 核心初始化: 后端 / 文件区 / SYSTEM_OS
        config.users_permission = True
        config.root_permission = (role == 'root')
        config.root = SYSTEM_OS(config.system_name, user, role)
        set_backend(PySide6Backend(self))

        self.append_output('Better terminal (PySide6)\n输入 help 查看命令。')

        global _CURRENT_WINDOW
        _CURRENT_WINDOW = self

    # ---- 换肤: theme 命令执行后由 apply_theme_now() 调用 ----
    def apply_theme(self):
        colors = theme.get_theme()
        self.setStyleSheet(theme.stylesheet(colors))
        self.output.setTextColor(colors['fg'])

    # ---- 输出 ----
    def append_output(self, text, prompt=False):
        colors = theme.get_theme()
        cursor = self.output.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        if prompt:
            self.output.setTextColor(colors['prompt'])
        else:
            self.output.setTextColor(colors['fg'])
        self.output.append(str(text))
        self.output.setTextCursor(cursor)
        self.output.ensureCursorVisible()

    def clear_output(self):
        self.output.clear()

    def submit(self):
        line = self.cmdline.text().strip()
        self.cmdline.clear()
        if not line:
            return
        self.cmdline.push(line)
        self.append_output('>>> ' + line, prompt=True)
        try:
            out = run_cmd_text(line)
        except SystemExit:
            raise
        except BaseException as e:
            self.append_output('内部错误: %s: %s' % (type(e).__name__, e))
            return
        if out:
            self.append_output(out)


def apply_theme_now():
    """theme 命令的回调: 立即把当前打开的终端窗口换上新配色。
    若窗口尚未打开 (例如纯 CLI 模式), 则仅更新 config.theme 留待下次启动生效。"""
    if _CURRENT_WINDOW is not None:
        try:
            _CURRENT_WINDOW.apply_theme()
            return True
        except Exception:
            return False
    return True


def launch():
    """创建登录窗口并进入事件循环"""
    colors = theme.get_theme()
    config.run_path = APP_DIR
    config.cmd_path = APP_DIR

    app = QApplication(sys.argv)
    app.setStyleSheet(theme.stylesheet(colors))

    holder = {}
    windows = []

    def on_login(name, role):
        win = TerminalWindow(name, role)
        windows.append(win)          # 保引用, 防止被 GC 提前析构
        win.show()
        login = holder.get('login')
        if login is not None:
            login.close()

    login = LoginWindow(on_login)
    holder['login'] = login
    login.show()
    sys.exit(app.exec())
