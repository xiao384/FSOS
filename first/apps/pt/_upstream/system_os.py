# ============================================================
# system_os.py — 系统操作模块
# 包含: SYSTEM_OS 类
#   - message / help       : 系统信息与帮助
#   - turn_off_system      : 关机(root)
#   - open_file            : 打开并编辑文件
#   - users 系列           : 用户管理
#   - sudo_permission      : sudo 提权
#   - auto_output          : 自动输出
# ============================================================
import json
import os
import platform
import subprocess
import random
import tkinter as tk
import time
from tkinter import messagebox

import shutil
import pyautogui
import pyperclip

import config
import theme

class SYSTEM_OS:
    def __init__(self, __system_name__, users_name, users_permission_detailed):
        self.system_name = __system_name__
        self.users_name = users_name
        self.users_permission_detailed = users_permission_detailed

    @property
    def message(self):
        cpu_info = platform.processor() or platform.machine()
        result_message = (f'系统:{platform.platform()}\nCPU:{platform.machine()}--{cpu_info}\n'
                          f'用户:{self.users_name}\n权限:{self.users_permission_detailed}')
        return result_message

    @property
    def help(self):
        cmd_list = [
            'help: 帮助界面',
            'message: 系统信息',
            'turn_off_system: 关机(root)',
            'open <路径>: 打开并编辑文件',
            'users: 查看当前用户信息',
            'users list: 列出所有用户',
            'users modify <用户名>: 修改用户密码/权限 (root)',
            'users add: 添加新用户 (root)',
            'users delete <用户名>: 删除用户 (root)',
            'sudo root: 获得root权限(永久)',
            'sudo <命令>: 以root权限临时执行一条命令',
            'Auto-output <选择1,文件内容;2,输入内容>: 自动输出内容',
            'reboot bash:重启终端(需重新登录)',
            'clear:清空终端',
            'time:获取现在时间',
            'echo <文本>: 输出文本',
            'history:输出历史命令',
            'ls:返回当前目录下的文件和文件夹',
            'cd<路径>:切换工作路径',
            'pwd:显示读取工作目录',
            'mkdir<路径>:创建目录',
            'rm <文件>: 删除文件(需root,移入回收站)',
            'rm -r <目录>: 递归删除目录(需root,移入回收站)',
        ]
        return '\n'.join(cmd_list)

    # 重启终端
    @property
    def reboot_bash(self):
        # 延迟导入 ui，避免与 ui.py 产生循环依赖
        from ui import reboot
        return reboot()
    # 清空终端
    def clear(self):
        """清空终端输出区"""
        text_widget = config.return_text_tk
        if text_widget is None:
            return '终端尚未初始化'
        text_widget.config(state='normal')
        text_widget.delete('1.0', 'end')
        text_widget.config(state='disabled')
        return '终端已清空'
    #显示时间
    def time(self):
        formatted_time = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
        return f'现在时间是:{formatted_time}'
    # echo 功能 — 输出文本（所有用户可用，便于测试和脚本化）
    def echo(self, text):
        """输出给定文本"""
        return text
    def history(self):
        from commands import history
        return history()
    #ls:显示工作目录下的文件和文件夹
    def ls(self):
        a = os.listdir(config.cmd_path)
        return f'工作路径: {config.cmd_path}\n' + '目录下的文件和文件夹:\n' + '\n'.join(a)
    def pwd(self):
        return config.cmd_path
    # ==================== rm：安全删除 ====================
    def _resolve_path(self, raw):
        """将相对路径基于当前工作路径解析为绝对路径"""
        raw = (raw or '').strip()
        if not raw:
            return ''
        return os.path.abspath(os.path.join(config.cmd_path, raw))

    def _is_dangerous_path(self, target):
        """判断目标是否为受保护的系统路径，返回 True 表示危险（禁止删除）"""
        target = os.path.normcase(os.path.abspath(target))
        run_path = os.path.normcase(os.path.abspath(config.__run_path__))

        # 1) 文件系统/磁盘根目录（如 C:\ 或 /）
        if os.path.dirname(target) == target:
            return True

        # 2) 程序自身目录或其任意父目录（删除会导致程序被删除）
        if target == run_path or run_path.startswith(target + os.sep):
            return True

        # 3) 系统关键目录黑名单
        system_dirs = {
            r'c:\windows', r'c:\windows\system32', r'c:\program files',
            r'c:\program files (x86)', '/etc', '/usr', '/bin', '/sbin',
            '/boot', '/lib', '/lib64', '/sys', '/proc', '/dev',
        }
        for d in system_dirs:
            if target == os.path.normcase(d):
                return True
        return False

    def _move_to_trash(self, target):
        """将目标移入回收站(.trash 目录)，避免不可恢复的删除"""
        trash_dir = os.path.join(config.__run_path__, '.trash')
        os.makedirs(trash_dir, exist_ok=True)
        name = os.path.basename(os.path.normpath(target)) or 'root'
        stamp = time.strftime('%Y%m%d_%H%M%S')
        dest = os.path.join(trash_dir, f'{name}@{stamp}')
        if os.path.exists(dest):
            dest = os.path.join(trash_dir, f'{name}@{stamp}_{random.randint(1000, 9999)}')
        shutil.move(target, dest)

    def rm(self, path):
        """安全删除文件或目录（需 root 权限，删除前二次确认，可回收）
        用法:
          rm <文件>        删除单个文件（移入回收站）
          rm -r <目录>     递归删除目录（移入回收站）
        """
        if not config.root_permission:
            return '权限不足：删除操作需要 root 权限'

        raw = (path or '').strip()
        if not raw:
            return '用法: rm <文件路径> 或 rm -r <目录路径>'

        # 解析标志与目标路径（maxsplit 保留路径中的空格）
        recursive = False
        target = raw
        parts = raw.split(maxsplit=1)
        if parts[0] in ('-r', '-rf', '-f'):
            if len(parts) < 2:
                return '用法: rm -r <目录路径>'
            recursive = True
            target = parts[1].strip()

        # 去除成对引号（支持 "路径 含空格" / '路径 含空格'）
        if len(target) >= 2 and target[0] == target[-1] and target[0] in ('"', "'"):
            target = target[1:-1]
        if not target:
            return '用法: rm <文件路径> 或 rm -r <目录路径>'

        target = self._resolve_path(target)
        if not target:
            return '用法: rm <文件路径> 或 rm -r <目录路径>'

        if self._is_dangerous_path(target):
            return f'拒绝删除：{target} 是受保护的系统路径，已阻止该操作'

        if not os.path.exists(target):
            return f'路径不存在: {target}'

        is_dir = os.path.isdir(target)
        if is_dir and not recursive:
            return f'{target} 是目录，如需递归删除请使用: rm -r {target}'

        # 二次确认
        confirmed = [False]
        C = theme.get_theme()
        win = tk.Toplevel(config.main_tk)
        win.title('确认删除')
        win.geometry('380x150')
        win.resizable(False, False)
        win.configure(bg=C['bg'])
        kind = '目录' if is_dir else '文件'
        theme.label(win, f'确认要删除{kind} "{target}" 吗？', C,
                    font=('', 10)).pack(pady=12)
        theme.label(win, '删除后可在回收站(.trash)中恢复', C,
                    fg=C['error'], font=('', 8)).pack()

        def do_delete():
            confirmed[0] = True
            win.destroy()

        btn_frame = tk.Frame(win, bg=C['bg'])
        btn_frame.pack(pady=12)
        theme.button(btn_frame, '确认删除', do_delete, C, width=12,
                     bg='#f44336').pack(side='left', padx=10)
        theme.button(btn_frame, '取消', win.destroy, C, width=12).pack(side='left', padx=10)

        win.grab_set()
        config.main_tk.wait_window(win)

        if not confirmed[0]:
            return '已取消删除'

        try:
            self._move_to_trash(target)
        except Exception as e:
            return f'删除失败: {e}'
        return f'已删除{kind}（移入回收站）: {target}'
    # 关机功能
    @property
    def turn_off_system(self):
        # 根据操作系统选择关机命令
        system = platform.system()
        if self.users_permission_detailed == 'root':
            if system == "Windows":
                shutdown_cmd = ["shutdown", "/s", "/t", "1"]
            elif system == "Linux":
                shutdown_cmd = ["shutdown", "now"]
            elif system == "Darwin":  # macOS
                shutdown_cmd = ["osascript", "-e",
                                'tell app "System Events" to shut down']
            else:
                return f"当前系统 ({system}) 暂不支持关机命令"
        else:
            return "权限不足：关机操作需要 root 权限"
        # 使用列表在闭包中传递用户确认结果
        confirmed = [False]
        def finally_turn_off():
            confirmed[0] = True
            turn_off_confirm.destroy()
            try:
                subprocess.run(shutdown_cmd, check=True)
            except subprocess.CalledProcessError as e:
                messagebox.showerror("关机失败",
                    f"命令执行失败: {e}\n请检查是否有足够权限。")
        def cancel_turn_off():
            turn_off_confirm.destroy()
        C = theme.get_theme()
        turn_off_confirm = tk.Toplevel(config.main_tk)
        turn_off_confirm.title('确认关机')
        turn_off_confirm.geometry('250x120')
        turn_off_confirm.resizable(False, False)
        turn_off_confirm.configure(bg=C['bg'])
        theme.label(turn_off_confirm, f'确认要关机吗？\n({system})', C,
                    font=('', 10)).pack(pady=10)
        btn_frame = tk.Frame(turn_off_confirm, bg=C['bg'])
        btn_frame.pack(pady=10)
        theme.button(btn_frame, '确认关机', finally_turn_off, C,
                     bg='#c42b1c', width=8).pack(side='left', padx=10)
        theme.button(btn_frame, '取消', cancel_turn_off, C,
                     width=8).pack(side='left', padx=10)
        # 阻塞等待用户确认
        turn_off_confirm.grab_set()
        config.main_tk.wait_window(turn_off_confirm)
        if confirmed[0]:
            return '系统正在关机...'
        else:
            return '已取消关机'

    # 切换工作路径
    def cd(self, new_path):
        new_path = (new_path or '').strip()
        if not new_path:
            return f'用法: cd <目录路径>\n当前工作路径: {config.cmd_path}'
        # 相对路径基于当前工作路径解析，abspath 归一化 . 与 ..
        target = os.path.abspath(os.path.join(config.cmd_path, new_path))
        if not os.path.isdir(target):
            return f'路径不存在或不是目录: {target}'
        config.cmd_path = target
        return f'工作路径已切换为: {config.cmd_path}'
    def mkdir(self,wjj_path):
        wjj_path = (wjj_path or '').strip()
        if not wjj_path:
            return '用法: mkdir <目录名>'
        target = os.path.abspath(os.path.join(config.cmd_path, wjj_path))
        if os.path.exists(target):
            return f'目录已存在: {target}'
        try:
            os.makedirs(target)
        except Exception as e:
            return f'创建目录失败: {e}'
        return f'目录已创建: {target}'
    # 文件打开功能
    def open_file(self, file_path):
        if not os.path.exists(file_path):
            return f'文件不存在: {file_path}'
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            return f'读取文件失败: {e}'
        # 创建文件编辑窗口
        C = theme.get_theme()
        open_file_tk = tk.Toplevel(config.main_tk)
        open_file_tk.title(file_path)
        open_file_tk.geometry('500x400')
        open_file_tk.configure(bg=C['bg'])
        # 菜单栏
        menubar = tk.Menu(open_file_tk, bg=C['bg'], fg=C['fg'],
                          activebackground=C['button_active'], activeforeground=C['fg'])
        file_menu = tk.Menu(menubar, tearoff=0,
                            bg=C['bg'], fg=C['fg'],
                            activebackground=C['button_active'], activeforeground=C['fg'])
        def save_file():
            try:
                new_content = text_widget.get("1.0", "end-1c")
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                messagebox.showinfo('提示', f'文件已保存: {file_path}')
            except Exception as e:
                messagebox.showerror('保存失败', str(e))
        def close_window():
            open_file_tk.destroy()
        file_menu.add_command(label='保存 (Ctrl+S)', command=save_file)
        file_menu.add_separator()
        file_menu.add_command(label='关闭', command=close_window)
        menubar.add_cascade(label='文件', menu=file_menu)
        open_file_tk.config(menu=menubar)
        # 文本编辑区
        text_widget = theme.text(open_file_tk, C, wrap='word', font=('Consolas', 10))
        text_widget.insert('1.0', content)
        text_widget.pack(fill='both', expand=True, padx=5, pady=5)
        # Ctrl+S 快捷键
        open_file_tk.bind('<Control-s>', lambda e: save_file())
        return f'已打开文件: {file_path}'

    # ==================== 用户管理 ====================
    def _load_users_data(self):
        """加载 users_data.json 数据"""
        with open(os.path.join(config.__run_path__, 'users_data.json'), 'r', encoding='utf-8') as f:
            return json.load(f)

    def _save_users_data(self, data):
        """保存数据到 users_data.json"""
        with open(os.path.join(config.__run_path__, 'users_data.json'), 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=4)

    def users(self, args_str):
        """用户管理入口
        用法:
          users                 → 查看当前用户信息
          users list            → 列出所有用户
          users modify <用户名>  → 修改用户密码/权限 (需 root)
          users add             → 添加新用户 (需 root)
          users delete <用户名>  → 删除用户 (需 root)
        """
        args = args_str.split() if args_str.strip() else []

        # ---- 无参数: 当前用户信息 ----
        if not args:
            return (f'当前用户: {self.users_name}\n'
                    f'权限: {self.users_permission_detailed}')

        sub_cmd = args[0]

        # ---- list: 列出所有用户 ----
        if sub_cmd == 'list':
            try:
                data = self._load_users_data()
            except Exception as e:
                return f'读取用户数据失败: {e}'
            lines = ['=== 用户列表 ===']
            for i in range(len(data['users_name'])):
                marker = ' ← 当前' if data['users_name'][i] == self.users_name else ''
                lines.append(f"  {data['users_name'][i]:12s}  权限: {data['users_root'][i]}{marker}")
            return '\n'.join(lines)

        # ---- 需要 root 的操作 ----
        if not config.root_permission:
            return '权限不足：修改用户信息需要 root 权限'

        if sub_cmd == 'modify':
            target = args[1] if len(args) > 1 else ''
            if not target:
                return '用法: users modify <用户名>'
            return self._modify_user_gui(target)

        elif sub_cmd == 'add':
            return self._add_user_gui()

        elif sub_cmd == 'delete':
            target = args[1] if len(args) > 1 else ''
            if not target:
                return '用法: users delete <用户名>'
            return self._delete_user(target)

        return (f'未知子命令: {sub_cmd}\n'
                f'可用: list | modify <用户名> | add | delete <用户名>')

    def _modify_user_gui(self, target):
        """GUI: 修改用户密码和权限"""
        try:
            data = self._load_users_data()
        except Exception as e:
            return f'读取用户数据失败: {e}'

        if target not in data['users_name']:
            return f'用户 "{target}" 不存在'

        idx = data['users_name'].index(target)

        # ---- 创建修改窗口 ----
        C = theme.get_theme()
        win = tk.Toplevel(config.main_tk)
        win.title(f'修改用户: {target}')
        win.geometry('340x280')
        win.resizable(False, False)
        win.configure(bg=C['bg'])

        # 标题
        title_frame = tk.Frame(win, bg=C['bg'])
        title_frame.pack(fill='x', pady=10)
        theme.label(title_frame, '正在修改用户: ', C,
                    font=('', 10)).pack(side='left')
        theme.label(title_frame, target, C,
                    font=('', 10, 'bold'), fg='#2196F3').pack(side='left')

        # 新密码
        theme.label(win, '新密码 (留空则不修改):', C).pack(anchor='w', padx=20)
        pwd_entry = theme.entry(win, C, show='*', width=35)
        pwd_entry.pack(pady=5)

        # 确认密码
        theme.label(win, '确认新密码:', C).pack(anchor='w', padx=20)
        pwd_confirm_entry = theme.entry(win, C, show='*', width=35)
        pwd_confirm_entry.pack(pady=5)

        # 权限选择
        theme.label(win, '用户权限:', C).pack(anchor='w', padx=20, pady=(10, 0))
        perm_var = tk.StringVar(value=data['users_root'][idx])
        perm_frame = tk.Frame(win, bg=C['bg'])
        perm_frame.pack(pady=5)
        theme.radiobutton(perm_frame, 'root  (管理员)', perm_var, 'root', C,
                          font=('', 9)).pack(side='left', padx=15)
        theme.radiobutton(perm_frame, 'users (普通用户)', perm_var, 'users', C,
                          font=('', 9)).pack(side='left', padx=15)

        # 提示当前权限
        theme.label(win, f'当前密码: {data["users_password"][idx]}', C,
                    fg='#888', font=('', 8)).pack(pady=(5, 0))

        def do_save():
            new_pwd = pwd_entry.get()
            pwd_confirm = pwd_confirm_entry.get()

            # 密码校验
            if new_pwd:
                if new_pwd != pwd_confirm:
                    messagebox.showerror('错误', '两次输入的密码不一致')
                    return
                data['users_password'][idx] = new_pwd

            data['users_root'][idx] = perm_var.get()

            try:
                self._save_users_data(data)
            except Exception as e:
                messagebox.showerror('保存失败', str(e))
                return

            # 如果修改了当前登录用户, 同步更新运行时状态
            if target == self.users_name:
                self.users_permission_detailed = perm_var.get()
                config.root_permission = (perm_var.get() == 'root')

            messagebox.showinfo('成功', f'用户 "{target}" 已更新')
            win.destroy()

        btn_frame = tk.Frame(win, bg=C['bg'])
        btn_frame.pack(pady=15)
        theme.button(btn_frame, '保存修改', do_save, C, width=12,
                     bg='#4CAF50').pack(side='left', padx=10)
        theme.button(btn_frame, '取消', win.destroy, C, width=12).pack(side='left', padx=10)

        win.grab_set()
        config.main_tk.wait_window(win)
        return f'用户 "{target}" 修改完成'

    def _add_user_gui(self):
        """GUI: 添加新用户"""
        C = theme.get_theme()
        win = tk.Toplevel(config.main_tk)
        win.title('添加新用户')
        win.geometry('340x400')
        win.resizable(False, False)
        win.configure(bg=C['bg'])

        theme.label(win, '创建新用户', C, font=('', 11, 'bold')).pack(pady=10)

        # 用户名
        theme.label(win, '用户名:', C).pack(anchor='w', padx=20)
        name_entry = theme.entry(win, C, width=35)
        name_entry.pack(pady=5)

        # 密码
        theme.label(win, '密码:', C).pack(anchor='w', padx=20)
        pwd_entry = theme.entry(win, C, show='*', width=35)
        pwd_entry.pack(pady=5)

        # 确认密码
        theme.label(win, '确认密码:', C).pack(anchor='w', padx=20)
        pwd_confirm_entry = theme.entry(win, C, show='*', width=35)
        pwd_confirm_entry.pack(pady=5)

        # 权限
        theme.label(win, '用户权限:', C).pack(anchor='w', padx=20, pady=(10, 0))
        perm_var = tk.StringVar(value='users')
        perm_frame = tk.Frame(win, bg=C['bg'])
        perm_frame.pack(pady=5)
        theme.radiobutton(perm_frame, 'root  (管理员)', perm_var, 'root', C,
                          font=('', 9)).pack(side='left', padx=15)
        theme.radiobutton(perm_frame, 'users (普通用户)', perm_var, 'users', C,
                          font=('', 9)).pack(side='left', padx=15)

        def do_add():
            new_name = name_entry.get().strip()
            new_pwd = pwd_entry.get()
            pwd_confirm = pwd_confirm_entry.get()

            if not new_name:
                messagebox.showerror('错误', '用户名不能为空')
                return
            if not new_pwd:
                messagebox.showerror('错误', '密码不能为空')
                return
            if new_pwd != pwd_confirm:
                messagebox.showerror('错误', '两次输入的密码不一致')
                return
            try:
                data = self._load_users_data()
            except Exception as e:
                messagebox.showerror('错误', f'读取用户数据失败: {e}')
                return

            if new_name in data['users_name']:
                messagebox.showerror('错误', f'用户 "{new_name}" 已存在')
                return

            data['users_name'].append(new_name)
            data['users_password'].append(new_pwd)
            data['users_root'].append(perm_var.get())

            try:
                self._save_users_data(data)
            except Exception as e:
                messagebox.showerror('保存失败', str(e))
                return

            messagebox.showinfo('成功', f'用户 "{new_name}" 已创建')
            win.destroy()

        btn_frame = tk.Frame(win, bg=C['bg'])
        btn_frame.pack(pady=15)
        theme.button(btn_frame, '添加用户', do_add, C, width=12,
                     bg='#4CAF50').pack(side='left', padx=10)
        theme.button(btn_frame, '取消', win.destroy, C, width=12).pack(side='left', padx=10)

        win.grab_set()
        config.main_tk.wait_window(win)
        return '用户添加操作已完成'

    def _delete_user(self, target):
        """删除用户"""
        try:
            data = self._load_users_data()
        except Exception as e:
            return f'读取用户数据失败: {e}'

        if target not in data['users_name']:
            return f'用户 "{target}" 不存在'

        if target == self.users_name:
            return '不能删除当前登录的用户'

        idx = data['users_name'].index(target)

        # 确认对话框
        confirmed = [False]
        C = theme.get_theme()
        win = tk.Toplevel(config.main_tk)
        win.title('确认删除')
        win.geometry('300x130')
        win.resizable(False, False)
        win.configure(bg=C['bg'])
        theme.label(win, f'确认要删除用户 "{target}" 吗？', C,
                    font=('', 10)).pack(pady=15)
        theme.label(win, '此操作不可撤销！', C, fg=C['error']).pack()

        def do_delete():
            confirmed[0] = True
            win.destroy()

        btn_frame = tk.Frame(win, bg=C['bg'])
        btn_frame.pack(pady=15)
        theme.button(btn_frame, '确认删除', do_delete, C, width=12,
                     bg='#f44336').pack(side='left', padx=10)
        theme.button(btn_frame, '取消', win.destroy, C, width=12).pack(side='left', padx=10)

        win.grab_set()
        config.main_tk.wait_window(win)

        if not confirmed[0]:
            return '已取消删除'

        del data['users_name'][idx]
        del data['users_password'][idx]
        del data['users_root'][idx]

        try:
            self._save_users_data(data)
        except Exception as e:
            return f'保存失败: {e}'

        return f'用户 "{target}" 已删除'

    # ==================== sudo 提权 ====================
    def _ask_root_password(self):
        """弹出窗口要求输入 root 密码，验证通过返回 True，否则返回 False"""
        result = [False]

        def sign_in_sudo():
            password_input = sudo_permission_tk_Entry.get()
            if not password_input:
                messagebox.showerror('错误', '请输入密码')
                return

            # 读取用户数据
            with open(os.path.join(config.__run_path__, 'users_data.json'),
                      'r', encoding='utf-8') as f:
                list_data = json.load(f)
            list_name = list_data['users_name']
            list_password = list_data['users_password']

            # 查找 root 用户密码
            root_pwd = None
            for i in range(len(list_name)):
                if list_name[i] == 'root':
                    root_pwd = list_password[i]
                    break

            if root_pwd is None:
                messagebox.showerror('错误', '系统中不存在 root 用户')
                return

            if password_input == root_pwd:
                result[0] = True
                sudo_permission_tk.destroy()
            else:
                messagebox.showerror('错误', 'root 密码错误')

        def cancel_sudo():
            sudo_permission_tk.destroy()

        C = theme.get_theme()
        sudo_permission_tk = tk.Toplevel(config.main_tk)
        sudo_permission_tk.geometry('260x160')
        sudo_permission_tk.resizable(False, False)
        sudo_permission_tk.title('sudo 提权')
        sudo_permission_tk.configure(bg=C['bg'])
        theme.label(sudo_permission_tk, '请输入 root 密码:', C).pack(pady=15)
        sudo_permission_tk_Entry = theme.entry(sudo_permission_tk, C, show='*')
        sudo_permission_tk_Entry.pack(pady=5)
        sudo_permission_tk_Entry.bind('<Return>', lambda e: sign_in_sudo())
        btn_frame = tk.Frame(sudo_permission_tk, bg=C['bg'])
        btn_frame.pack(pady=10)
        theme.button(btn_frame, '确认', sign_in_sudo, C,
                     width=10, bg="#4CAF50").pack(side='left', padx=10)
        theme.button(btn_frame, '取消', cancel_sudo, C,
                     width=10).pack(side='left', padx=10)
        sudo_permission_tk.grab_set()
        config.main_tk.wait_window(sudo_permission_tk)
        return result[0]

    def sudo_permission(self, account):
        """sudo root — 输入 root 密码永久获得管理员权限"""
        if account != 'root':
            return '用法: sudo root'
        if config.root_permission:
            return '当前已是 root 权限，无需再次提权'
        if self._ask_root_password():
            config.root_permission = True
            self.users_permission_detailed = 'root'
            # 同步更新窗口标题
            config.main_tk.title(f'bash-{config.__system_name__}--{self.users_name}--root')
            return '成功获得 root 权限'
        return '已取消'

    def sudo_run(self, command_text):
        """sudo <命令> — 输入 root 密码后，以 root 权限临时执行单条命令
        用法:
          sudo root       永久获得 root 权限
          sudo <命令>     以 root 权限临时执行一条命令（执行后权限自动恢复）
        """
        command_text = (command_text or '').strip()
        if not command_text:
            return '用法: sudo root 或 sudo <命令>'
        if command_text == 'root':
            return self.sudo_permission('root')
        if config.root_permission:
            # 已是 root 权限，直接执行
            from commands import run_cmd_text
            return run_cmd_text(command_text)
        if command_text.lower().startswith('sudo'):
            return '不支持在 sudo 中嵌套 sudo'

        if not self._ask_root_password():
            return '已取消'

        # 临时提权执行单条命令，执行后恢复原权限
        old_root = config.root_permission
        old_detailed = self.users_permission_detailed
        config.root_permission = True
        self.users_permission_detailed = 'root'
        try:
            from commands import run_cmd_text
            return run_cmd_text(command_text)
        finally:
            config.root_permission = old_root
            self.users_permission_detailed = old_detailed

    # Auto-output 功能
    def _resolve_auto_output_file(self, raw_path):
        """解析自动输出文件路径。

        规则:
          1. 是存在的文件 → 直接使用
          2. 是目录       → 列出目录中的文件供用户选择
          3. 仅文件名     → 在工作路径 (config.cmd_path) 下查找
        返回: (文件路径或 None, 错误信息或 None)
        """
        raw_path = raw_path.strip()
        if not raw_path:
            return None, '请输入文件路径'

        # 1) 直接是存在的文件
        if os.path.isfile(raw_path):
            return raw_path, None

        # 2) 是目录 → 列出目录中的文件供用户选择
        if os.path.isdir(raw_path):
            files = sorted(
                f for f in os.listdir(raw_path)
                if os.path.isfile(os.path.join(raw_path, f))
            )
            if not files:
                return None, f'目录内没有可输出的文件: {raw_path}'
            selected = self._pick_file_from_dir(raw_path, files)
            return (selected, None) if selected is not None else (None, '已取消')

        # 3) 仅文件名（无目录成分）→ 在工作路径下查找
        if os.path.dirname(raw_path) == '':
            candidate = os.path.join(config.cmd_path, raw_path)
            if os.path.isfile(candidate):
                return candidate, None
            return None, (f'工作路径下未找到文件: {raw_path}\n'
                          f'工作路径: {config.cmd_path}')

        # 4) 相对路径/绝对路径均不存在
        return None, f'文件不存在: {raw_path}'

    def _pick_file_from_dir(self, directory, files):
        """弹出窗口让用户从目录中的文件里选择一个。返回选中的完整路径或 None。"""
        C = theme.get_theme()
        pick_tk = tk.Toplevel(config.main_tk)
        pick_tk.title('选择要输出的文件')
        pick_tk.geometry('420x320')
        pick_tk.resizable(False, False)
        pick_tk.configure(bg=C['bg'])

        theme.label(pick_tk, f'目录: {directory}\n请选择要输出的文件:', C,
                    justify='left').pack(pady=(10, 5), padx=10, anchor='w')

        listbox = tk.Listbox(pick_tk, bg=C['entry_bg'], fg=C['fg'],
                             selectbackground=C['button_active'],
                             selectforeground=C['fg'], height=10)
        listbox.pack(fill='both', expand=True, padx=15, pady=5)
        for name in files:
            listbox.insert('end', name)

        result = [None]

        def confirm():
            sel = listbox.curselection()
            if sel:
                result[0] = os.path.join(directory, files[sel[0]])
            pick_tk.destroy()

        def cancel():
            pick_tk.destroy()

        # 双击列表项直接确认
        listbox.bind('<Double-Button-1>', lambda e: confirm())

        btn_frame = tk.Frame(pick_tk, bg=C['bg'])
        btn_frame.pack(pady=10)
        theme.button(btn_frame, '确认', confirm, C, bg='#4CAF50',
                     width=10).pack(side='left', padx=10)
        theme.button(btn_frame, '取消', cancel, C,
                     width=10).pack(side='left', padx=10)

        pick_tk.grab_set()
        config.main_tk.wait_window(pick_tk)
        return result[0]

    # Auto-output 功能
    def auto_output(self, choose):
        """自动输出 — 模拟键盘在终端中粘贴文本
        用法: Auto-output 1  (从文件读取)
              Auto-output 2  (手动输入内容)
        """
        if choose not in ('1', '2'):
            return '用法: Auto-output <1(文件内容) 或 2(手动输入)>'

        result = [None]  # 存储 (text, count, delay)

        def start_auto_output():
            # 1. 读取次数和倒计时
            try:
                count = int(auto_output_tk_Entry2.get())
                delay = int(auto_output_tk_Entry3.get())
            except ValueError:
                messagebox.showerror('错误', '次数和倒计时必须为整数')
                return
            if count <= 0 or delay < 0:
                messagebox.showerror('错误', '次数必须 > 0，倒计时必须 >= 0')
                return

            # 2. 读取文本内容
            if choose == '1':
                file_path, err = self._resolve_auto_output_file(
                    auto_output_tk_Entry.get())
                if err:
                    messagebox.showerror('错误', err)
                    return
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        text = f.read()
                except Exception as e:
                    messagebox.showerror('错误', f'读取文件失败: {e}')
                    return
            else:  # choose == '2'
                text = auto_output_tk_Entry.get()
                if not text.strip():
                    messagebox.showerror('错误', '请输入要输出的内容')
                    return

            # 3. 关闭窗口，传递结果
            auto_output_tk.destroy()
            result[0] = (text, count, delay)

        # ---- 创建配置窗口 ----
        C = theme.get_theme()
        auto_output_tk = tk.Toplevel(config.main_tk)
        auto_output_tk.title('Auto-output')
        auto_output_tk.geometry('450x350')
        auto_output_tk.resizable(False, False)
        auto_output_tk.configure(bg=C['bg'])

        if choose == '1':
            theme.label(auto_output_tk, '请输入文件路径、目录或文件名:', C).pack(pady=(15, 5))
            auto_output_tk_Entry = theme.entry(auto_output_tk, C, width=50)
            auto_output_tk_Entry.pack(pady=5)
        else:
            theme.label(auto_output_tk, '请输入要自动输出的内容:', C).pack(pady=(15, 5))
            auto_output_tk_Entry = theme.entry(auto_output_tk, C, width=50)
            auto_output_tk_Entry.pack(pady=5)

        theme.label(auto_output_tk, '请输入输出次数:', C).pack(pady=(15, 5))
        auto_output_tk_Entry2 = theme.entry(auto_output_tk, C, width=20)
        auto_output_tk_Entry2.pack(pady=5)
        auto_output_tk_Entry2.insert(0, '1')

        theme.label(auto_output_tk, '请输入开始倒计时(秒):', C).pack(pady=(15, 5))
        auto_output_tk_Entry3 = theme.entry(auto_output_tk, C, width=20)
        auto_output_tk_Entry3.pack(pady=5)
        auto_output_tk_Entry3.insert(0, '3')

        theme.button(auto_output_tk, '开始自动输出', start_auto_output, C,
                     bg='#2196F3', width=15).pack(pady=20)
        theme.label(auto_output_tk, '提示: 点击后切换到目标输入框等待', C,
                    fg='#888', font=('', 8)).pack()

        auto_output_tk.grab_set()
        config.main_tk.wait_window(auto_output_tk)

        # ---- 执行自动化 ----
        if result[0] is None:
            return '已取消'

        text, count, delay = result[0]
        # 倒计时（GUI 会暂时冻结）
        for remaining in range(delay, 0, -1):
            time.sleep(1)
        # 执行循环粘贴
        pyperclip.copy(text)
        for i in range(count):
            time.sleep(random.uniform(0.2, 0.5))
            pyautogui.hotkey('ctrl', 'v')
            pyautogui.press('enter')
        return f'自动输出完成，共输出 {count} 次'

