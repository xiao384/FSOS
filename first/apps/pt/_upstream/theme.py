# ============================================================
# theme.py — 主题模块（暗色 / 亮色，自动跟随系统主题）
#   - detect_system_theme() : 检测系统亮/暗主题
#   - get_theme()           : 获取当前主题配色字典
#   - 小部件创建助手         : window / label / entry / button /
#                             radiobutton / text（统一套用配色）
# ============================================================
import os
import sys
import tkinter as tk

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
        'prompt': '#098658',        # 亮色提示符(绿)
        'error': '#c42b1c',         # 亮色错误(红)
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
    elif sys.platform == 'darwin':  # macOS：读取系统偏好
        try:
            import subprocess
            out = subprocess.check_output(
                ['defaults', 'read', '-g', 'AppleInterfaceStyle']
            ).decode().strip()
            return 'dark' if out == 'Dark' else 'light'
        except Exception:
            return 'light'
    return 'light'  # 其他系统默认亮色


def get_theme():
    """返回当前系统主题对应的配色字典"""
    return THEMES.get(detect_system_theme(), THEMES['light'])


# ---- 小部件创建助手（统一套用主题配色）----
def window(parent, title, width, height, colors):
    """创建带主题配色的 Toplevel 窗口"""
    win = tk.Toplevel(parent)
    win.title(title)
    win.geometry(f'{width}x{height}')
    win.configure(bg=colors['bg'])
    return win


def label(parent, text, colors, **kw):
    bg = kw.pop('bg', colors['bg'])
    fg = kw.pop('fg', colors['fg'])
    return tk.Label(parent, text=text, bg=bg, fg=fg, **kw)


def entry(parent, colors, **kw):
    bg = kw.pop('bg', colors['entry_bg'])
    fg = kw.pop('fg', colors['fg'])
    return tk.Entry(parent, bg=bg, fg=fg,
                    insertbackground=colors['cursor'], **kw)


def button(parent, text, command, colors, **kw):
    bg = kw.pop('bg', colors['button_bg'])
    fg = kw.pop('fg', colors['fg'])
    return tk.Button(parent, text=text, command=command,
                     bg=bg, fg=fg,
                     activebackground=colors['button_active'],
                     activeforeground=colors['fg'], **kw)


def radiobutton(parent, text, variable, value, colors, **kw):
    bg = kw.pop('bg', colors['bg'])
    fg = kw.pop('fg', colors['fg'])
    return tk.Radiobutton(parent, text=text, variable=variable, value=value,
                          bg=bg, fg=fg,
                          selectcolor=colors['entry_bg'],
                          activebackground=colors['bg'],
                          activeforeground=colors['fg'], **kw)


def text(parent, colors, **kw):
    bg = kw.pop('bg', colors['entry_bg'])
    fg = kw.pop('fg', colors['fg'])
    return tk.Text(parent, bg=bg, fg=fg,
                   insertbackground=colors['cursor'], **kw)
