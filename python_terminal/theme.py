# ============================================================
# theme.py — 主题模块（暗色 / 亮色，自动跟随系统主题）
#   - detect_system_theme() : 检测系统亮/暗主题
#   - get_theme()           : 获取当前主题配色字典
#   - 小部件创建助手         : window / label / entry / button /
#                             radiobutton / text（统一套用配色）
# 注: 已从 tkinter 迁移到 PySide6（tk -> PySide6）。
# ============================================================
import os
import sys

from PySide6.QtWidgets import (QDialog, QLabel, QLineEdit, QPushButton,
                               QRadioButton, QTextEdit)
from PySide6.QtGui import QFont, QPalette, QColor
from PySide6.QtCore import Qt

# 主题配色表
THEMES = {
    'dark': {
        'name': 'dark',
        'bg': '#1e1e1e',            # 窗口主背景
        'entry_bg': '#2d2d2d',      # 输入框/文本区背景
        'button_bg': '#3c3c3c',     # 按钮背景
        'button_active': '#505050', # 按钮按下
        'fg': '#ffffff',            # 前景文字
        'cursor': '#ffffff',        # 光标颜色
        'prompt': '#6a9955',        # 提示符(绿)
        'error': '#f44747',         # 错误(红)
    },
    'light': {
        'name': 'light',
        'bg': '#f3f3f3',
        'entry_bg': '#ffffff',
        'button_bg': '#e1e1e1',
        'button_active': '#cfcfcf',
        'fg': '#1e1e1e',
        'cursor': '#000000',
        'prompt': '#098658',
        'error': '#c42b1c',
    },
}


def detect_system_theme():
    """检测系统主题，返回 'dark' 或 'light'"""
    if os.name == 'nt':  # Windows：读取注册表
        try:
            import winreg
            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r'Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
            ) as key:
                value, _ = winreg.QueryValueEx(key, 'AppsUseLightTheme')
            return 'light' if value == 1 else 'dark'
        except OSError:
            return 'dark'  # 读取失败时默认深色
    elif sys.platform == 'darwin':
        try:
            import subprocess
            out = subprocess.check_output(
                ['defaults', 'read', '-g', 'AppleInterfaceStyle']
            ).decode().strip()
            return 'dark' if out == 'Dark' else 'light'
        except Exception:
            return 'light'
    return 'light'


def get_theme():
    """返回当前系统主题对应的配色字典"""
    return THEMES.get(detect_system_theme(), THEMES['light'])


def _apply_font(w, kw):
    font = kw.pop('font', None)
    if font:
        if isinstance(font, tuple):
            fam = font[0] if len(font) > 0 else ''
            size = font[1] if len(font) > 1 else 10
            w.setFont(QFont(fam, size))
        else:
            w.setFont(QFont(str(font)))


def _style(bg, fg):
    return f"background-color:{bg};color:{fg};"


def window(parent, title, width, height, colors):
    """创建带主题配色的对话框窗口（替代 tk.Toplevel）"""
    win = QDialog(parent)
    win.setWindowTitle(title)
    win.resize(width, height)
    # 仅设置窗口自身背景，避免通用 QWidget{} 样式表污染子控件导致渲染条纹
    pal = win.palette()
    pal.setColor(QPalette.Window, QColor(colors['bg']))
    win.setPalette(pal)
    win.setAutoFillBackground(True)
    return win


def label(parent, text, colors, **kw):
    bg = kw.pop('bg', colors['bg'])
    fg = kw.pop('fg', colors['fg'])
    w = QLabel(text, parent)
    w.setStyleSheet(_style(bg, fg))
    _apply_font(w, kw)
    return w


def entry(parent, colors, **kw):
    bg = kw.pop('bg', colors['entry_bg'])
    fg = kw.pop('fg', colors['fg'])
    w = QLineEdit(parent)
    if kw.pop('show', None) == '*':
        w.setEchoMode(QLineEdit.Password)
    w.setStyleSheet(_style(bg, fg))
    _apply_font(w, kw)
    return w


def button(parent, text, command, colors, **kw):
    bg = kw.pop('bg', colors['button_bg'])
    fg = kw.pop('fg', colors['fg'])
    w = QPushButton(text, parent)
    w.clicked.connect(command)
    w.setStyleSheet(_style(bg, fg))
    _apply_font(w, kw)
    return w


def radiobutton(parent, text, colors, **kw):
    """PySide6 单选按钮。value 通过 setProperty('value', ...) 附加，
    由调用方用 QButtonGroup 管理并读取。"""
    bg = kw.pop('bg', colors['bg'])
    fg = kw.pop('fg', colors['fg'])
    value = kw.pop('value', None)
    w = QRadioButton(text, parent)
    w.setStyleSheet(_style(bg, fg))
    _apply_font(w, kw)
    if value is not None:
        w.setProperty('value', value)
    return w


def text(parent, colors, **kw):
    bg = kw.pop('bg', colors['entry_bg'])
    fg = kw.pop('fg', colors['fg'])
    w = QTextEdit(parent)
    w.setStyleSheet(_style(bg, fg))
    _apply_font(w, kw)
    return w
