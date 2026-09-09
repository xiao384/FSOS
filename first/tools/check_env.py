# check_env.py - 检查本仓库的 Better terminal 改造版能否在"系统裸 Python"下运行
#
# 背景: 原项目 (D:\better terminal_project\pt) 依赖 pyautogui / pyperclip / tkinter
#       等第三方库 (见 _upstream/system_os.py), 需要它的 .venv 才能跑。
#       本仓库改造版 (apps/pt) 已把这些替换为后端接口 + 标准库, 仅:
#         - 标准库: json / os / platform / subprocess / random / shutil / sys /
#                   time / winreg (这些系统 Python 自带, 无需 venv)
#         - PySide6 : 仅宿主机图形界面 (host/) 需要, 可选
#         - pyautogui / pyperclip : 仅 host 的 "auto-output 2" (桌面粘贴) 延迟导入,
#                                   缺失时优雅降级, 不影响其它功能
#         - 内核版 (pyroot/bt.py) : 由 MicroPython 执行, 不依赖任何上述宿主库
#
# 用法:  python tools/check_env.py

import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIRST = os.path.dirname(HERE)
PYROOT = os.path.join(FIRST, 'pyroot', 'bt.py')
UPSTREAM = os.path.join(FIRST, 'apps', 'pt', '_upstream')


def line(t):
    print(t)


def check_stdlib(mods):
    line('[1] 标准库 (系统 Python 自带, 无需 venv)')
    ok = True
    for m in mods:
        try:
            importlib.import_module(m)
            line('    OK   %-12s' % m)
        except Exception as e:
            ok = False
            line('    FAIL %-12s : %s' % (m, e))
    return ok


def check_pyside6():
    line('[2] PySide6 (仅宿主机图形界面需要, 内核版不需要)')
    try:
        import PySide6  # noqa
        from PySide6.QtWidgets import QApplication  # noqa
        line('    OK   PySide6 已安装 -> 可运行宿主图形终端 (host/main_host.py)')
        return True
    except Exception as e:
        line('    SKIP PySide6 未安装: 宿主图形终端需 "pip install PySide6"')
        line('         内核版 (bt 命令) 与纯 CLI 版不受影响')
        return False


def check_optional():
    line('[3] 可选增强: pyautogui / pyperclip (仅 "auto-output 2" 桌面粘贴用, 缺失可降级)')
    for m in ('pyautogui', 'pyperclip'):
        try:
            importlib.import_module(m)
            line('    OK   %-12s' % m)
        except Exception:
            line('    SKIP %-12s 未安装 -> auto-output 2 将返回提示, 其它正常' % m)


def check_bundled_has_no_thirdparty():
    line('[4] 拼合产物 pyroot/bt.py 是否混入第三方 import')
    banned = ('pyautogui', 'pyperclip', 'tkinter', 'PySide6', 'cv2', 'numpy',
              'requests', 'PIL')
    if not os.path.isfile(PYROOT):
        line('    SKIP pyroot/bt.py 不存在 (先运行 tools/bundle_pt.py --frozen)')
        return True
    bad = []
    with open(PYROOT, 'r', encoding='utf-8') as f:
        for i, ln in enumerate(f, 1):
            s = ln.strip()
            if s.startswith('#'):
                continue
            for b in banned:
                # 顶层 import <b> / from <b> import
                if s.startswith('import %s' % b) or s.startswith('from %s' % b):
                    bad.append((i, b))
    if bad:
        for i, b in bad:
            line('    FAIL 第 %d 行含 %s' % (i, b))
        return False
    line('    OK   无第三方 import (内核 MicroPython 可直接执行)')
    return True


def check_upstream_untouched():
    line('[5] 源项目副本 (_upstream) 完整性提示')
    if not os.path.isdir(UPSTREAM):
        line('    SKIP _upstream 不存在')
        return True
    # 源项目依赖第三方库, 仅作提示, 不计入失败
    line('    注意: _upstream 是原项目镜像, 内含 pyautogui/pyperclip/tkinter,')
    line('          需要原项目的 .venv 才能运行 -> 本仓库不使用这份镜像运行。')
    return True


def main():
    line('=' * 64)
    line(' Better terminal 改造版 - 运行环境自检')
    line('=' * 64)
    line('Python: %s' % sys.version.split()[0])
    line('')

    stdlib = ['json', 'os', 'platform', 'subprocess', 'random',
              'shutil', 'sys', 'time', 'winreg']
    r1 = check_stdlib(stdlib)
    line('')
    r2 = check_pyside6()
    line('')
    check_optional()
    line('')
    r4 = check_bundled_has_no_thirdparty()
    line('')
    check_upstream_untouched()
    line('')
    line('=' * 64)
    if r1 and r4:
        line(' 结论: 核心功能可运行 (标准库齐全, 拼合产物无第三方依赖)')
        if not r2:
            line('       图形界面需另装 PySide6: pip install PySide6')
        line(' 建议: 无需复制 .venv 中的库到系统; 内核版 (bt) 由 MicroPython 运行。')
        return 0
    line(' 结论: 存在阻碍, 请按上方 FAIL 项处理。')
    return 1


if __name__ == '__main__':
    sys.exit(main())
