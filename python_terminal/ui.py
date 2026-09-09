# ============================================================
# ui.py — 图形界面模块（PySide6 版，已由 tkinter 迁移）
#   - sign_in() : 登录验证
#   - main()    : 终端主窗口（主题跟随系统）
#   - launch()  : 创建登录窗口并启动主循环
#   - reboot()  : 重启终端（重新登录）
# ============================================================
import json
import os
import platform
import sys
import subprocess

from PySide6.QtWidgets import (QApplication, QWidget, QVBoxLayout,
                               QLineEdit, QTextEdit, QLabel, QMessageBox)
from PySide6.QtCore import QTimer, Qt, QEvent, QObject
from PySide6.QtGui import QPalette, QColor

import config
import theme
import commands
from commands import run_cmd_text
from system_os import SYSTEM_OS

# 应用级事件过滤器：拦截 Tab（Qt 的焦点遍历会在按键到达控件前吞掉 Tab，
# 导致 keyPressEvent 收不到），用于命令补全
term_filter = None


class _TermKeyFilter(QObject):
    def eventFilter(self, obj, e):
        if e.type() == QEvent.KeyPress and isinstance(obj, TerminalInput):
            if e.key() == Qt.Key_Tab:
                if obj._onComplete:
                    obj._onComplete(obj.text())
                e.accept()
                return True
        return False


def _clear_layout(layout):
    """递归清空布局内所有控件（用于复用登录窗口作为终端窗口）"""
    if layout is None:
        return
    while layout.count():
        item = layout.takeAt(0)
        w = item.widget()
        if w is not None:
            w.deleteLater()
        else:
            _clear_layout(item.layout())


def sign_in():
    # 账户与密码读取
    with open(os.path.join(config.__run_path__, 'users_data.json'), 'r', encoding='utf-8') as f:
        list_data = json.load(f)
        list_name = list_data['users_name']
        list_password = list_data['users_password']
        list_root = list_data['users_root']
    input_name = config.tk_input_name.text()
    input_password = config.tk_input_key.text()

    # 逐用户匹配用户名与密码，避免认证绕过
    config.users_permission = False
    config.root_permission = False  # 重置，避免上次登录的 root 权限残留
    users_permission_detailed = ''
    for i in range(len(list_name)):
        if list_name[i] == input_name and list_password[i] == input_password:
            config.users_permission = True
            users_permission_detailed = list_root[i]
            if list_root[i] == 'root':
                config.root_permission = True
            break

    if config.users_permission:
        config.root = SYSTEM_OS(config.__system_name__, input_name, users_permission_detailed)
        # 延后切换窗口：等当前按钮/回车回调完全返回后，再重建终端，
        # 避免在按钮自身的 slot 里销毁按钮导致事件循环异常
        QTimer.singleShot(0, _diagnostic_main)
    else:
        QMessageBox.critical(config.sign_tk, '错误', '用户名或密码错误')


class TerminalInput(QLineEdit):
    """终端输入框：↑/↓ 历史、Tab 补全、Ctrl+L 清屏、Ctrl+C 取消当前行/密码输入"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._onClear = None
        self._onComplete = None
        self._onCancel = None
        self._getHistory = None
        self._hist_index = None

    def keyPressEvent(self, e):
        key = e.key()
        mods = e.modifiers()
        if key == Qt.Key_Up:
            self._nav(-1)
            e.accept()
        elif key == Qt.Key_Down:
            self._nav(1)
            e.accept()
        elif key == Qt.Key_Tab:
            if self._onComplete:
                self._onComplete(self.text())
            e.accept()
        elif mods == Qt.ControlModifier and key == Qt.Key_L:
            if self._onClear:
                self._onClear()
            e.accept()
        elif mods == Qt.ControlModifier and key == Qt.Key_C:
            if self._onCancel:
                self._onCancel()
            self.clear()
            e.accept()
        else:
            super().keyPressEvent(e)

    def _nav(self, direction):
        if config.sudo_state is not None:
            return
        hist = self._getHistory() if self._getHistory else []
        if not hist:
            return
        if self._hist_index is None:
            self._hist_index = len(hist)
        self._hist_index += direction
        if self._hist_index < 0:
            self._hist_index = 0
        if self._hist_index >= len(hist):
            self._hist_index = len(hist)
            self.clear()
            return
        self.setText(hist[self._hist_index])
        self.setCursorPosition(len(self.text()))


def _is_error(text):
    """粗略判断命令输出是否为错误/失败信息（用于标红）"""
    if not text:
        return False
    prefixes = ('错误', '权限不足', '用法', '拒绝', '不存在', '失败', '未存在',
                '文件不存在', '目录不存在', '读取文件失败', '保存失败', '解压失败',
                '安装失败', '复制失败', '移动失败', '删除失败', '写入失败')
    return text.startswith(prefixes)


def main():
    if not config.users_permission:
        return
    C = theme.get_theme()
    # 复用登录窗口的根窗口作为终端主窗口，避免二次创建顶层窗口
    config.main_tk = config.sign_tk
    win = config.main_tk

    _clear_layout(win.layout())

    win.setWindowTitle(
        f'bash-{platform.system()}{platform.release()}--'
        f'{config.root.users_name}--{config.root.users_permission_detailed}')
    win.resize(700, 600)
    _set_window_bg(win, C['bg'])

    layout = win.layout()

    return_text = theme.text(win, C)
    return_text.setReadOnly(True)
    return_text.setStyleSheet(
        f"background-color:{C['entry_bg']};color:{C['fg']};"
        f"font-family:'Consolas','Courier New',monospace;")
    layout.addWidget(return_text)
    config.return_text_tk = return_text

    def prompt_str():
        sym = '#' if config.root_permission else '$'
        return f"{config.root.users_name}@psos:{config.cmd_path}{sym} "

    def _update_title():
        config.main_tk.setWindowTitle(
            f'bash-{platform.system()}{platform.release()}--'
            f'{config.root.users_name}--{config.root.users_permission_detailed}')

    def append_line(text, color=None):
        return_text.setTextColor(QColor(color if color else C['fg']))
        return_text.append(text)
        sb = return_text.verticalScrollBar()
        sb.setValue(sb.maximum())

    # 欢迎横幅
    banner = (
        f"=== PSOS 终端 ===\n"
        f"用户: {config.root.users_name}   权限: {config.root.users_permission_detailed}\n"
        f"{'root 模式：可执行全部系统命令' if config.root_permission else '普通用户：部分命令需 root 提权'}\n"
        f"支持: ↑/↓ 历史, Tab 补全, Ctrl+L 清屏, Ctrl+C 取消, exit 退出\n"
        f"输入 help 查看可用命令\n"
    )
    append_line(banner, C['prompt'])

    entry_cmd = TerminalInput(win)
    layout.addWidget(entry_cmd)

    def on_complete(text):
        if ' ' in text:
            return
        cands = [c for c in commands.COMMAND_NAMES if c.startswith(text)]
        if not cands:
            return
        if len(cands) == 1:
            entry_cmd.setText(cands[0] + ' ')
        else:
            pref = os.path.commonprefix(cands)
            if pref and pref != text:
                entry_cmd.setText(pref)
            append_line('  '.join(cands), C['prompt'])

    def on_submit():
        text_input = entry_cmd.text().strip()
        entry_cmd._hist_index = None
        entry_cmd.clear()

        # ---- sudo 密码输入模式 ----
        if config.sudo_state is not None:
            if not text_input:
                append_line('', C['fg'])
                return
            # 不记录密码到历史
            append_line('[sudo] password for root:', C['prompt'])
            ok = config.root._verify_root_password(text_input)
            if not ok:
                config.sudo_state['attempts'] += 1
                if config.sudo_state['attempts'] >= 3:
                    append_line('sudo: 3 次密码错误，已取消', C['error'])
                    config.sudo_state = None
                    entry_cmd.setEchoMode(QLineEdit.Normal)
                else:
                    append_line('sudo: 密码错误，请重试', C['error'])
                entry_cmd.setFocus()
                return

            mode = config.sudo_state['mode']
            saved_cmd = config.sudo_state['command_text']
            config.sudo_state = None
            entry_cmd.setEchoMode(QLineEdit.Normal)

            if mode == 'root':
                config.root_permission = True
                config.root.users_permission_detailed = 'root'
                _update_title()
                append_line('成功获得 root 权限', C['prompt'])
            elif mode == 'run':
                result = config.root._execute_sudo_run(saved_cmd)
                # 若命令触发了登出（窗口已重建），立即停止访问旧控件
                if not config.users_permission:
                    return
                if result:
                    append_line(result, C['error'] if _is_error(result) else None)
            entry_cmd.setFocus()
            return

        # ---- 普通命令模式 ----
        if text_input:
            config.cmd_history.append(text_input)
        if not text_input:
            append_line(prompt_str(), C['prompt'])
            return
        append_line(prompt_str() + text_input, C['prompt'])
        result = run_cmd_text(text_input)
        # 若命令触发了登出（窗口已重建），立即停止访问旧控件
        if not config.users_permission:
            return
        if result:
            append_line(result, C['error'] if _is_error(result) else None)
        # 如果 sudo 命令要求输入密码，切换到密码输入模式
        if config.sudo_state is not None:
            entry_cmd.setEchoMode(QLineEdit.Password)
        entry_cmd.setFocus()

    def cancel_input():
        if config.sudo_state is not None:
            config.sudo_state = None
            entry_cmd.setEchoMode(QLineEdit.Normal)
            append_line('sudo: 已取消', C['error'])
        entry_cmd.clear()
        entry_cmd.setFocus()

    entry_cmd._onClear = return_text.clear
    entry_cmd._onComplete = on_complete
    entry_cmd._onCancel = cancel_input
    entry_cmd._getHistory = lambda: config.cmd_history
    entry_cmd.returnPressed.connect(on_submit)
    entry_cmd.setFocus()
    print('[ui] main 构建完成', flush=True)


def _diagnostic_main():
    """临时诊断：捕获 main() 内任何异常，定位进程退出原因"""
    try:
        main()
    except BaseException as e:
        import traceback
        print(f'[ui] main 回调抛出 {type(e).__name__}: {e!r}', flush=True)
        traceback.print_exc()


def _set_window_bg(win, color):
    """仅给窗口本身设置背景色，不影响内部子控件（避免通用 QWidget 样式表导致渲染条纹）"""
    pal = win.palette()
    pal.setColor(QPalette.Window, QColor(color))
    win.setPalette(pal)
    win.setAutoFillBackground(True)


def show_login():
    """构建/重建登录窗口（launch 与 logout 共用）"""
    C = theme.get_theme()
    if config.sign_tk is None:
        config.sign_tk = QWidget()
    win = config.sign_tk
    win.setWindowTitle('shell_open')
    win.resize(300, 200)
    _set_window_bg(win, C['bg'])
    if win.layout():
        _clear_layout(win.layout())
    layout = QVBoxLayout(win)

    first_label = theme.label(win, "输入用户名和密码登录：", C)
    layout.addWidget(first_label)
    config.tk_input_name = theme.entry(win, C)
    layout.addWidget(config.tk_input_name)
    config.tk_input_key = theme.entry(win, C, show='*')
    layout.addWidget(config.tk_input_key)
    first_button = theme.button(win, 'submit', sign_in, C)
    layout.addWidget(first_button)

    config.tk_input_name.returnPressed.connect(sign_in)
    config.tk_input_key.returnPressed.connect(sign_in)

    win.show()


def logout():
    """退出登录：重置状态并返回登录界面"""
    config.users_permission = False
    config.root_permission = False
    config.root = None
    config.cmd_history = []
    if config.main_tk is not None and config.main_tk.layout():
        _clear_layout(config.main_tk.layout())
    show_login()


def launch():
    """创建登录窗口并启动主循环"""
    print('[ui] 单窗口版启动')
    app = QApplication.instance() or QApplication(sys.argv)
    global term_filter
    term_filter = _TermKeyFilter(app)
    app.installEventFilter(term_filter)
    config.sign_tk = None
    show_login()
    app.exec()


def reboot():
    """重启终端：销毁当前窗口，重新启动进程（需重新登录）"""
    entry = sys.argv[0] if sys.argv else ''
    if not entry or not os.path.isfile(os.path.abspath(entry)):
        entry = os.path.join(config.__run_path__, 'main.py')
    entry = os.path.abspath(entry)

    popen_kwargs = dict(
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if os.name == 'nt':
        popen_kwargs['creationflags'] = (
            subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
        )
    try:
        subprocess.Popen([sys.executable, entry], **popen_kwargs)
    except Exception as e:
        QMessageBox.critical(config.main_tk, '重启失败', f'无法启动新进程: {e}')
        return '重启失败'

    for win in (config.main_tk, config.sign_tk):
        if win is not None:
            try:
                win.close()
            except Exception:
                pass
    QApplication.instance().quit()
    os._exit(0)
