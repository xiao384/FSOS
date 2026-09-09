# ============================================================
# backend_pyside6.py - PySide6 界面后端
#
# 对应 _upstream/system_os.py 里散落各处的 tk 对话框:
#   tk.Toplevel + wait_window  -> QDialog.exec()
#   messagebox.showinfo/error  -> QMessageBox.information/warning
#   tk.Entry(show='*')         -> QInputDialog (Password) / QLineEdit
#   tk.Listbox                 -> QListWidget
#   文本编辑窗口               -> QTextEdit
#
# 宿主机专有能力 (桌面自动化) 放在 paste(): 依赖 pyperclip / pyautogui,
# 缺失时给出提示而不是让命令崩溃。
# ============================================================
import os
import subprocess
import sys

from PySide6.QtWidgets import (QApplication, QDialog, QDialogButtonBox,
                               QFormLayout, QInputDialog, QLabel, QLineEdit,
                               QListWidget, QMessageBox, QTextEdit,
                               QVBoxLayout)

from core.backend import Backend


class PySide6Backend(Backend):
    name = 'pyside6'

    def __init__(self, window=None):
        self.window = window

    # ---- 输出 ----
    def out(self, text):
        if self.window is not None:
            self.window.append_output(text)
        else:
            print(text)

    def clear(self):
        if self.window is not None:
            self.window.clear_output()

    def set_title(self, title):
        if self.window is not None:
            self.window.setWindowTitle(title)

    # ---- 提示 ----
    def notify(self, title, msg):
        QMessageBox.information(self.window, title, msg)

    def warn(self, title, msg):
        QMessageBox.warning(self.window, title, msg)

    def confirm(self, title, msg, danger=False):
        box = QMessageBox(self.window)
        box.setWindowTitle(title)
        box.setText(msg)
        if danger:
            box.setIcon(QMessageBox.Icon.Warning)
        else:
            box.setIcon(QMessageBox.Icon.Question)
        box.setStandardButtons(QMessageBox.StandardButton.Yes |
                               QMessageBox.StandardButton.No)
        box.setDefaultButton(QMessageBox.StandardButton.No)
        return box.exec() == QMessageBox.StandardButton.Yes

    def ask_text(self, title, prompt, secret=False):
        if secret:
            text, ok = QInputDialog.getText(self.window, title, prompt,
                                            QLineEdit.EchoMode.Password)
        else:
            text, ok = QInputDialog.getText(self.window, title, prompt)
        if not ok:
            return None
        return text

    def ask_fields(self, title, fields):
        dlg = QDialog(self.window)
        dlg.setWindowTitle(title)
        layout = QFormLayout(dlg)
        edits = {}
        for key, label, secret in fields:
            edit = QLineEdit(dlg)
            if secret:
                edit.setEchoMode(QLineEdit.EchoMode.Password)
            layout.addRow(label, edit)
            edits[key] = edit
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                   QDialogButtonBox.StandardButton.Cancel, dlg)
        buttons.accepted.connect(dlg.accept)
        buttons.rejected.connect(dlg.reject)
        layout.addRow(buttons)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return None
        result = {}
        for key in edits:
            result[key] = edits[key].text()
        return result

    def pick(self, title, options):
        dlg = QDialog(self.window)
        dlg.setWindowTitle(title)
        dlg.resize(420, 320)
        layout = QVBoxLayout(dlg)
        layout.addWidget(QLabel('请选择:', dlg))
        listw = QListWidget(dlg)
        for opt in options:
            listw.addItem(str(opt))
        layout.addWidget(listw)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                   QDialogButtonBox.StandardButton.Cancel, dlg)
        buttons.accepted.connect(dlg.accept)
        buttons.rejected.connect(dlg.reject)
        layout.addWidget(buttons)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return None
        return listw.currentRow()

    def edit_text(self, title, content):
        dlg = QDialog(self.window)
        dlg.setWindowTitle(title)
        dlg.resize(600, 460)
        layout = QVBoxLayout(dlg)
        editor = QTextEdit(dlg)
        editor.setPlainText(content)
        layout.addWidget(editor)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Save |
                                   QDialogButtonBox.StandardButton.Cancel, dlg)
        buttons.accepted.connect(dlg.accept)
        buttons.rejected.connect(dlg.reject)
        layout.addWidget(buttons)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return None
        return editor.toPlainText()

    # ---- 桌面自动化 ----
    def paste(self, text, count, delay):
        try:
            import pyautogui
            import pyperclip
        except ImportError:
            return '自动输出需要 pyperclip 与 pyautogui (pip install pyperclip pyautogui)'
        import random
        import time
        pyperclip.copy(text)
        for _ in range(delay):
            time.sleep(1)
        try:
            for _ in range(count):
                time.sleep(random.uniform(0.2, 0.5))
                pyautogui.hotkey('ctrl', 'v')
                pyautogui.press('enter')
        except Exception as e:
            return '自动输出中断: ' + str(e)
        return '自动输出完成, 共 ' + str(count) + ' 次'

    # ---- 重启终端 ----
    def reboot_app(self):
        entry = sys.argv[0] if sys.argv else ''
        if not entry or not os.path.isfile(os.path.abspath(entry)):
            entry = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 'main_host.py')
        entry = os.path.abspath(entry)
        kwargs = dict(stdin=subprocess.DEVNULL,
                      stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL)
        if os.name == 'nt':
            kwargs['creationflags'] = (subprocess.DETACHED_PROCESS |
                                       subprocess.CREATE_NEW_PROCESS_GROUP)
        try:
            subprocess.Popen([sys.executable, entry], **kwargs)
        except Exception as e:
            return '重启失败: ' + str(e)
        app = QApplication.instance()
        if app is not None:
            app.quit()
        return '正在重启终端...'
