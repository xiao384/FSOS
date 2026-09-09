# ============================================================
# theme.py - PySide6 版主题
# 由 _upstream/theme.py (tkinter widget 工厂) 改造而来:
#   原文件导出 label/entry/button/text 等一系列"套好配色的 tk 小部件工厂",
#   tk 强依赖因此无法复用。PySide6 的惯例是把配色交给 QPalette / QSS,
#   所以这里改为导出配色表 + 一份 QSS 样式表, 由各窗口统一应用。
# ============================================================
import os
import sys

THEMES = {
    'dark': {
        'name': 'dark',
        'bg': '#1e1e1e',            # 窗口主背景
        'entry_bg': '#2d2d2d',      # 输入区/文本区背景
        'button_bg': '#3c3c3c',     # 按钮背景
        'button_active': '#505050',  # 按钮按下
        'fg': '#ffffff',            # 前景文字
        'cursor': '#ffffff',        # 光标颜色
        'prompt': '#6a9955',        # 提示符(绿)
        'error': '#f44747',         # 错误(红)
        'border': '#3c3c3c',
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
        'border': '#c8c8c8',
    },
}


def detect_system_theme():
    """检测系统亮/暗主题, 返回 'dark' 或 'light'"""
    if os.name == 'nt':
        try:
            import winreg
            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r'Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
            ) as key:
                value, _ = winreg.QueryValueEx(key, 'AppsUseLightTheme')
            return 'light' if value == 1 else 'dark'
        except OSError:
            return 'dark'
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
    """返回当前主题配色字典。

    优先级: 手动调用 set_theme() 设定的主题 > 跟随系统自动检测。
    这样 'theme dark' / 'theme light' 命令可以在运行时固定配色,
    而不必每次都跟随操作系统设置。
    """
    try:
        from core.config import config
        override = getattr(config, 'theme', None)
    except ImportError:
        override = None
    if override in THEMES:
        return THEMES[override]
    return THEMES.get(detect_system_theme(), THEMES['light'])


def set_theme(name):
    """手动设定主题 ('dark' / 'light'), 返回是否成功"""
    if name not in THEMES:
        return False
    try:
        from core.config import config
        config.theme = name
    except ImportError:
        pass
    return True


def available_themes():
    """列出可用主题名, 供 theme 命令的 help/校验使用"""
    return list(THEMES.keys())


def stylesheet(c):
    """生成整窗 QSS: 一处应用, 所有子控件自动跟随"""
    return (
        'QWidget {'
        '  background-color: %s;'
        '  color: %s;'
        '}'
        'QLineEdit, QTextEdit, QListWidget {'
        '  background-color: %s;'
        '  color: %s;'
        '  border: 1px solid %s;'
        '  selection-background-color: %s;'
        '}'
        'QPushButton {'
        '  background-color: %s;'
        '  color: %s;'
        '  border: 1px solid %s;'
        '  padding: 4px 10px;'
        '}'
        'QPushButton:hover { background-color: %s; }'
        'QPushButton:pressed { background-color: %s; }'
        'QLabel { background: transparent; color: %s; }'
    ) % (c['bg'], c['fg'],
         c['entry_bg'], c['fg'], c['border'], c['button_active'],
         c['button_bg'], c['fg'], c['border'],
         c['button_active'], c['button_active'],
         c['fg'])
