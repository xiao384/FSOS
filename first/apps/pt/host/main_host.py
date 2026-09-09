# ============================================================
# main_host.py - 宿主机入口 (PySide6 图形终端)
#
# 用法:
#   pip install PySide6
#   python apps/pt/host/main_host.py
#
# 未安装 PySide6 时给出安装提示而不是抛栈; 只想跑逻辑可用
#   python apps/pt/host/main_host.py --cli
# 以纯命令行后端启动 (无需任何 GUI 依赖)。
# ============================================================
import os
import sys

APP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if APP_DIR not in sys.path:
    sys.path.insert(0, APP_DIR)


def run_gui():
    try:
        import PySide6  # noqa: F401
    except ImportError:
        print('未安装 PySide6。图形界面需要它:')
        print('  pip install PySide6')
        print('或者用纯命令行后端启动:')
        print('  python %s --cli' % os.path.abspath(__file__))
        return 1
    from host.ui_pyside6 import launch
    launch()
    return 0


def run_cli():
    """无 GUI 的命令行模式: 与内核文本后端行为一致, 便于对照测试"""
    from core.backend import set_backend, CliBackend
    from core.commands import run_cmd_text
    from core.config import config
    from core.fs_host import HostFS
    from core.ptos import SYSTEM_OS

    set_backend(CliBackend())
    config.run_path = APP_DIR
    config.fs = HostFS(os.getcwd())
    config.users_permission = True
    config.root_permission = True
    config.root = SYSTEM_OS(config.system_name, 'root', 'root', config.fs)

    print('Better terminal (CLI 后端) - 输入 help 查看命令, exit 退出。')
    while True:
        try:
            line = input('bt> ')
        except EOFError:
            break
        line = line.strip()
        if not line or line in ('exit', 'quit'):
            break
        out = run_cmd_text(line)
        if out:
            print(out)
    return 0


def main():
    if '--cli' in sys.argv:
        return run_cli()
    return run_gui()


if __name__ == '__main__':
    sys.exit(main())
