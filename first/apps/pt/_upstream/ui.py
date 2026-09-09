# ============================================================
# ui.py — 图形界面模块
#   - sign_in() : 登录验证
#   - main()    : 终端主窗口（主题跟随系统）
#   - launch()  : 创建登录窗口并启动主循环
#   - reboot()  : 重启终端（重新登录）
# ============================================================
import json
import os
import platform
import subprocess
import sys
import tkinter as tk
from tkinter import messagebox

import config
import theme
from commands import run_cmd_text
from system_os import SYSTEM_OS


def sign_in():
    # 账户与密码读取
    with open(os.path.join(config.__run_path__, 'users_data.json'), 'r', encoding='utf-8') as f:
        list_data = json.load(f)
        list_name = list_data['users_name']
        list_password = list_data['users_password']
        list_root = list_data['users_root']
    input_name = config.tk_input_name.get()
    input_password = config.tk_input_key.get()

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
        # 延后切换窗口：等当前按钮/回车回调完全返回后，再销毁登录控件并构建终端，
        # 避免在按钮自身的 command 回调里销毁按钮导致事件循环提前退出
        config.sign_tk.after(0, _diagnostic_main)
    else:
        messagebox.showerror('错误', '用户名或密码错误')


def main():
    print(config.users_permission, config.root_permission)
    if config.users_permission:
        C = theme.get_theme()
        # 复用登录窗口的根窗口作为终端主窗口，避免二次 tk.Tk()（第二次 Tk() 窗口可能不显示）
        config.main_tk = config.sign_tk
        for w in config.main_tk.winfo_children():
            w.destroy()
        config.main_tk.title(f'bash-{platform.system()}{platform.release()}--{config.root.users_name}--{config.root.users_permission_detailed}')
        config.main_tk.geometry('700x600')
        config.main_tk.configure(bg=C['bg'])
        return_text_tk = theme.text(config.main_tk, C, wrap='word', font=('Consolas', 10))
        return_text_tk.pack(fill='both', expand=True, padx=5, pady=5)
        return_text_tk.config(state='disabled')  # 禁止用户直接编辑
        config.return_text_tk = return_text_tk   # 保存引用，供 clear 命令清空
        # 文本着色标签：提示符 / 错误
        return_text_tk.tag_configure('prompt', foreground=C['prompt'])
        return_text_tk.tag_configure('error', foreground=C['error'])
        Entry_cmd = theme.entry(config.main_tk, C, font=('Consolas', 10))
        Entry_cmd.pack(side='left', fill='x', expand=True)

        def return_set_cmd():
            text_input = Entry_cmd.get()
            return_text = run_cmd_text(text_input)
            # 临时启用以写入输出
            return_text_tk.config(state='normal')
            return_text_tk.insert('end', f">>> {text_input}\n", 'prompt')
            return_text_tk.insert('end', return_text + '\n')
            return_text_tk.see('end')
            return_text_tk.config(state='disabled')
            Entry_cmd.delete(0, 'end')

        Entry_cmd.bind('<Return>', lambda e: return_set_cmd())
        print('[ui] main 构建完成', flush=True)
        # 不调用 mainloop()：launch() 里的 sign_tk.mainloop() 会继续处理终端窗口事件


def _diagnostic_main():
    """临时诊断：捕获 main() 内任何异常（含 SystemExit），定位进程退出原因"""
    try:
        main()
    except BaseException as e:
        import traceback
        print(f'[ui] main 回调抛出 {type(e).__name__}: {e!r}', flush=True)
        traceback.print_exc()


def launch():
    """创建登录窗口并启动主循环"""
    print('[ui] 单窗口版启动')  # 临时诊断：看到这行=运行的是修复后的代码
    C = theme.get_theme()
    config.sign_tk = tk.Tk()
    config.sign_tk.title('shell_open')
    config.sign_tk.geometry('300x200')
    config.sign_tk.configure(bg=C['bg'])
    first_label = theme.label(config.sign_tk, "输入用户名和密码登录：", C)
    first_label.pack()
    config.tk_input_name = theme.entry(config.sign_tk, C)
    config.tk_input_name.pack(pady=10)
    config.tk_input_key = theme.entry(config.sign_tk, C, show='*')
    config.tk_input_key.pack(pady=10)
    first_button = theme.button(config.sign_tk, 'submit', sign_in, C)
    first_button.pack(pady=10)
    # 回车键触发登录
    config.tk_input_name.bind('<Return>', lambda e: sign_in())
    config.tk_input_key.bind('<Return>', lambda e: sign_in())
    config.sign_tk.mainloop()
    try:
        still_exists = config.sign_tk.winfo_exists()
    except Exception:
        still_exists = '异常'
    print(f'[ui] mainloop 已返回; 窗口仍存在: {still_exists}', flush=True)


def reboot():
    """重启终端：销毁当前窗口，重新启动进程（需重新登录）"""
    # 确定入口脚本：优先 sys.argv[0]，定位不到时回退到本目录 main.py
    entry = sys.argv[0] if sys.argv else ''
    if not entry or not os.path.isfile(os.path.abspath(entry)):
        entry = os.path.join(config.__run_path__, 'main.py')
    entry = os.path.abspath(entry)

    # 先启动新进程；失败则保留当前窗口并提示，避免"窗口已关却重启失败"
    # 关键：不要让子进程继承父进程的 stdout/stderr。在 PyCharm 里父进程的
    # 控制台管道会在 os._exit 后断开，子进程若继承该管道，一 print 就崩溃，
    # 窗口还没出现进程就挂了。故重定向到 DEVNULL；Windows 再脱离进程组。
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
        messagebox.showerror('重启失败', f'无法启动新进程: {e}')
        return '重启失败'

    # 销毁窗口并立即退出当前进程
    for win in (config.main_tk, config.sign_tk):
        if win is not None:
            try:
                win.destroy()
            except Exception:
                pass
    # 用 os._exit 保证立即退出，避免 SystemExit 在 Tk 回调中被拦截导致进程残留
    os._exit(0)