# ============================================================
# main.py — 程序入口模块
# 说明: Better terminal_project.py 文件名含空格，无法被 import，
#       因此把入口逻辑放在本模块，由主程序文件调用 run_main()。
#   - run_main() : 启动终端（登录窗口）
# ============================================================
import atexit

from ui import launch

atexit.register(lambda: print('[main] atexit 进程退出', flush=True))


def run_main():
    """启动终端（登录窗口）"""
    try:
        launch()
    except BaseException as e:
        import traceback
        print(f'[main] 顶层捕获 {type(e).__name__}: {e!r}', flush=True)
        traceback.print_exc()


if __name__ == '__main__':
    run_main()
