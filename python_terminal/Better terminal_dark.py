# Better Terminal — 暗色主题版
import json
import os
import platform
import random
import subprocess
import time
import tkinter as tk
from tkinter import messagebox
import pyautogui
import pyperclip

# ==================== 暗色主题配色 ====================
BG_DARK       = '#1e1e1e'   # 主背景色
BG_ENTRY      = '#2d2d2d'   # 输入框/文本区背景
BG_BUTTON     = '#3c3c3c'   # 按钮背景
BG_BUTTON_ACT = '#505050'   # 按钮按下
FG_LIGHT      = '#ffffff'   # 文字颜色
CURSOR_COLOR  = '#ffffff'   # 光标颜色

# ==================== 系统检测 ====================
__system_name__ = os.name
__run_path__ = os.path.dirname(__file__)
root_permission = False
users_permission = False

# ==================== 系统操作类 ====================
class SYSTEM_OS:
    def __init__(self, __system_name__, users_name, users_permission_detailed):
        self.system_name = __system_name__
        self.users_name = users_name
        self.users_permission_detailed = users_permission_detailed

    @property
    def message(self):
        result_message = (f'系统:{platform.platform()}\n'
                          f'CPU:{platform.machine()}--{platform.processor()}\n'
                          f'用户:{self.users_name}\n'
                          f'权限:{self.users_permission_detailed}')
        return result_message

    @property
    def help(self):
        cmd_list = [
            'help: 帮助界面',
            'message: 系统信息',
            'turn_off_system: 关机',
            'open <路径>: 打开并编辑文件',
            'sudo root: 获得 root 权限',
            'Auto-output <1或2>: 自动输出内容(1=文件,2=手动)',
        ]
        return '\n'.join(cmd_list)

    @property
    def turn_off_system(self):
        if __system_name__ != "nt":
            return "当前系统暂不支持关机命令"
        confirmed = [False]

        def finally_turn_off():
            confirmed[0] = True
            turn_off_confirm.destroy()
            subprocess.run(["shutdown", "/s", "/t", "1"], check=True)

        def cancel_turn_off():
            turn_off_confirm.destroy()

        turn_off_confirm = tk.Toplevel(main_tk)
        turn_off_confirm.title('确认关机')
        turn_off_confirm.geometry('250x120')
        turn_off_confirm.resizable(False, False)
        turn_off_confirm.configure(bg=BG_DARK)

        tk.Label(turn_off_confirm, text='确认要关机吗？', font=('', 10),
                 bg=BG_DARK, fg=FG_LIGHT).pack(pady=10)

        btn_frame = tk.Frame(turn_off_confirm, bg=BG_DARK)
        btn_frame.pack(pady=10)

        tk.Button(btn_frame, text='确认关机', command=finally_turn_off,
                  bg='#c42b1c', fg=FG_LIGHT,
                  activebackground='#e03e2f', activeforeground=FG_LIGHT
                  ).pack(side='left', padx=10)
        tk.Button(btn_frame, text='取消', command=cancel_turn_off,
                  bg=BG_BUTTON, fg=FG_LIGHT,
                  activebackground=BG_BUTTON_ACT, activeforeground=FG_LIGHT
                  ).pack(side='left', padx=10)

        turn_off_confirm.grab_set()
        main_tk.wait_window(turn_off_confirm)
        if confirmed[0]:
            return '系统正在关机...'
        else:
            return '已取消关机'

    def open_file(self, file_path):
        if not os.path.exists(file_path):
            return f'文件不存在: {file_path}'
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            return f'读取文件失败: {e}'

        open_file_tk = tk.Toplevel(main_tk)
        open_file_tk.title(file_path)
        open_file_tk.geometry('500x400')
        open_file_tk.configure(bg=BG_DARK)

        # 菜单栏
        menubar = tk.Menu(open_file_tk, bg=BG_DARK, fg=FG_LIGHT,
                          activebackground=BG_BUTTON_ACT, activeforeground=FG_LIGHT)
        file_menu = tk.Menu(menubar, tearoff=0,
                            bg=BG_DARK, fg=FG_LIGHT,
                            activebackground=BG_BUTTON_ACT, activeforeground=FG_LIGHT)

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
        text_widget = tk.Text(open_file_tk, wrap='word', font=('Consolas', 10),
                              bg=BG_ENTRY, fg=FG_LIGHT, insertbackground=CURSOR_COLOR)
        text_widget.insert('1.0', content)
        text_widget.pack(fill='both', expand=True, padx=5, pady=5)

        open_file_tk.bind('<Control-s>', lambda e: save_file())
        return f'已打开文件: {file_path}'

    def sudo_permission(self, account):
        """sudo 提权 — 输入 root 密码获得管理员权限"""
        if account != 'root':
            return '用法: sudo root'

        result = [None]

        def sign_in_sudo():
            password_input = sudo_permission_tk_Entry.get()
            if not password_input:
                messagebox.showerror('错误', '请输入密码')
                return

            with open(os.path.join(__run_path__, 'users_data.json'),
                      'r', encoding='utf-8') as f:
                list_data = json.load(f)
            list_name = list_data['users_name']
            list_password = list_data['users_password']

            root_pwd = None
            for i in range(len(list_name)):
                if list_name[i] == 'root':
                    root_pwd = list_password[i]
                    break

            if root_pwd is None:
                messagebox.showerror('错误', '系统中不存在 root 用户')
                return

            if password_input == root_pwd:
                global root_permission
                root_permission = True
                self.users_permission_detailed = 'root'
                result[0] = '成功获得 root 权限'
                main_tk.title(f'bash_root:root')
                sudo_permission_tk.destroy()
            else:
                messagebox.showerror('错误', 'root 密码错误')

        def cancel_sudo():
            sudo_permission_tk.destroy()

        sudo_permission_tk = tk.Toplevel(main_tk)
        sudo_permission_tk.geometry('260x160')
        sudo_permission_tk.resizable(False, False)
        sudo_permission_tk.title('sudo 提权')
        sudo_permission_tk.configure(bg=BG_DARK)
        tk.Label(sudo_permission_tk, text='请输入 root 密码:',
                 bg=BG_DARK, fg=FG_LIGHT).pack(pady=15)
        sudo_permission_tk_Entry = tk.Entry(sudo_permission_tk, show='*',
                                            bg=BG_ENTRY, fg=FG_LIGHT,
                                            insertbackground=CURSOR_COLOR)
        sudo_permission_tk_Entry.pack(pady=5)
        sudo_permission_tk_Entry.bind('<Return>', lambda e: sign_in_sudo())
        btn_frame = tk.Frame(sudo_permission_tk, bg=BG_DARK)
        btn_frame.pack(pady=10)
        tk.Button(btn_frame, text='确认', command=sign_in_sudo,
                  width=10, bg='#4CAF50', fg=FG_LIGHT,
                  activebackground=BG_BUTTON_ACT,
                  activeforeground=FG_LIGHT).pack(side='left', padx=10)
        tk.Button(btn_frame, text='取消', command=cancel_sudo,
                  width=10, bg=BG_BUTTON, fg=FG_LIGHT,
                  activebackground=BG_BUTTON_ACT,
                  activeforeground=FG_LIGHT).pack(side='left', padx=10)
        sudo_permission_tk.grab_set()
        main_tk.wait_window(sudo_permission_tk)
        return result[0] if result[0] else '已取消'

    def auto_output(self, choose):
        """自动输出 — 模拟键盘在终端中粘贴文本"""
        if choose not in ('1', '2'):
            return '用法: Auto-output <1(文件内容) 或 2(手动输入)>'

        result = [None]

        def start_auto_output():
            try:
                count = int(auto_output_tk_Entry2.get())
                delay = int(auto_output_tk_Entry3.get())
            except ValueError:
                messagebox.showerror('错误', '次数和倒计时必须为整数')
                return
            if count <= 0 or delay < 0:
                messagebox.showerror('错误', '次数必须 > 0，倒计时必须 >= 0')
                return

            if choose == '1':
                file_path = auto_output_tk_Entry.get().strip()
                if not file_path:
                    messagebox.showerror('错误', '请输入文件路径')
                    return
                if not os.path.exists(file_path):
                    messagebox.showerror('错误', f'文件不存在: {file_path}')
                    return
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        text = f.read()
                except Exception as e:
                    messagebox.showerror('错误', f'读取文件失败: {e}')
                    return
            else:
                text = auto_output_tk_Entry.get()
                if not text.strip():
                    messagebox.showerror('错误', '请输入要输出的内容')
                    return

            auto_output_tk.destroy()
            result[0] = (text, count, delay)

        auto_output_tk = tk.Toplevel(main_tk)
        auto_output_tk.title('Auto-output')
        auto_output_tk.geometry('450x350')
        auto_output_tk.resizable(False, False)
        auto_output_tk.configure(bg=BG_DARK)

        if choose == '1':
            tk.Label(auto_output_tk, text='请输入要自动输出的文件路径:',
                     bg=BG_DARK, fg=FG_LIGHT).pack(pady=(15, 5))
            auto_output_tk_Entry = tk.Entry(auto_output_tk, width=50,
                                            bg=BG_ENTRY, fg=FG_LIGHT,
                                            insertbackground=CURSOR_COLOR)
            auto_output_tk_Entry.pack(pady=5)
        else:
            tk.Label(auto_output_tk, text='请输入要自动输出的内容:',
                     bg=BG_DARK, fg=FG_LIGHT).pack(pady=(15, 5))
            auto_output_tk_Entry = tk.Entry(auto_output_tk, width=50,
                                            bg=BG_ENTRY, fg=FG_LIGHT,
                                            insertbackground=CURSOR_COLOR)
            auto_output_tk_Entry.pack(pady=5)

        tk.Label(auto_output_tk, text='请输入输出次数:',
                 bg=BG_DARK, fg=FG_LIGHT).pack(pady=(15, 5))
        auto_output_tk_Entry2 = tk.Entry(auto_output_tk, width=20,
                                         bg=BG_ENTRY, fg=FG_LIGHT,
                                         insertbackground=CURSOR_COLOR)
        auto_output_tk_Entry2.pack(pady=5)
        auto_output_tk_Entry2.insert(0, '1')

        tk.Label(auto_output_tk, text='请输入开始倒计时(秒):',
                 bg=BG_DARK, fg=FG_LIGHT).pack(pady=(15, 5))
        auto_output_tk_Entry3 = tk.Entry(auto_output_tk, width=20,
                                         bg=BG_ENTRY, fg=FG_LIGHT,
                                         insertbackground=CURSOR_COLOR)
        auto_output_tk_Entry3.pack(pady=5)
        auto_output_tk_Entry3.insert(0, '3')

        tk.Button(auto_output_tk, text='开始自动输出', command=start_auto_output,
                  bg='#2196F3', fg=FG_LIGHT, width=15).pack(pady=20)
        tk.Label(auto_output_tk, text='提示: 点击后切换到目标输入框等待',
                 bg=BG_DARK, fg='#888', font=('', 8)).pack()

        auto_output_tk.grab_set()
        main_tk.wait_window(auto_output_tk)

        if result[0] is None:
            return '已取消'

        text, count, delay = result[0]
        for remaining in range(delay, 0, -1):
            time.sleep(1)
        pyperclip.copy(text)
        for i in range(count):
            time.sleep(random.uniform(0.2, 0.5))
            pyautogui.hotkey('ctrl', 'v')
            pyautogui.press('enter')
        return f'自动输出完成，共输出 {count} 次'


# ==================== 登录验证 ====================
def sign_in():
    global users_permission, root_permission
    i = 0
    with open(__run_path__ + '/users_data.json', 'r', encoding='utf-8') as f:
        list_data = json.load(f)
        list_name = list_data['users_name']
        list_password = list_data['users_password']
        list_root = list_data['users_root']
        print(list_name, list_password, list_root)

    input_name = tk_input_name.get()
    input_password = tk_input_key.get()

    if input_name in list_name and input_password in list_password:
        for i in range(len(list_name)):
            if list_name[i] == input_name:
                if list_password[i] == input_password:
                    users_permission = True
                    break
        else:
            users_permission = False
        users_permission_detailed = list_root[i]
        if list_root[i] == "root" and users_permission:
            root_permission = True
        global root
        root = SYSTEM_OS(__system_name__, input_name, users_permission_detailed)
    else:
        messagebox.showerror('错误', '用户不存在')
    main()


# ==================== 命令解析 ====================
def run_cmd_text(text_input):
    parts = text_input.split(maxsplit=1)
    cmd = parts[0]
    args = parts[1] if len(parts) > 1 else ''
    agree_run = ['turn_off_system', 'message', 'help', 'open', 'sudo', 'Auto-output']
    if cmd in agree_run:
        if cmd == 'help':
            return root.help
        elif cmd == 'turn_off_system':
            return root.turn_off_system
        elif cmd == 'message':
            return root.message
        elif cmd == 'open':
            if not args:
                return '用法: open <文件路径>'
            return root.open_file(args)
        elif cmd == 'sudo':
            return root.sudo_permission(args)
        elif cmd == 'Auto-output':
            return root.auto_output(args)
        return f'命令 "{cmd}" 暂未实现'
    else:
        return ('错误，未存在命令\n'
                '你可以输入help来查询可用命令')


# ==================== 主窗口 ====================
def main():
    global users_permission, root_permission, main_tk
    print(users_permission, root_permission)
    if users_permission:
        sign_tk.destroy()
        main_tk = tk.Tk()
        main_tk.title(f'bash_root:{root_permission}')
        main_tk.geometry('700x600')
        main_tk.configure(bg=BG_DARK)

        # 输出文本框
        return_text_tk = tk.Text(main_tk, wrap='word', font=('Consolas', 10),
                                 bg=BG_ENTRY, fg=FG_LIGHT, insertbackground=CURSOR_COLOR)
        return_text_tk.pack(fill='both', expand=True, padx=5, pady=5)
        return_text_tk.config(state='disabled')  # 禁止用户直接编辑

        # 定义文本标签颜色
        return_text_tk.tag_configure('prompt', foreground='#6a9955')   # 绿色提示符
        return_text_tk.tag_configure('error',  foreground='#f44747')   # 红色错误

        # 命令输入框
        Entry_cmd = tk.Entry(main_tk, font=('Consolas', 10),
                             bg=BG_ENTRY, fg=FG_LIGHT, insertbackground=CURSOR_COLOR)
        Entry_cmd.pack(side='left', fill='x', expand=True)

        def return_set_cmd():
            text_input = Entry_cmd.get()
            return_text = run_cmd_text(text_input)
            return_text_tk.config(state='normal')
            return_text_tk.insert('end', f">>> {text_input}\n", 'prompt')
            return_text_tk.insert('end', return_text + '\n')
            return_text_tk.see('end')
            return_text_tk.config(state='disabled')
            Entry_cmd.delete(0, 'end')

        Entry_cmd.bind('<Return>', lambda e: return_set_cmd())
        main_tk.mainloop()


# ==================== 登录窗口 ====================
sign_tk = tk.Tk()
sign_tk.title('shell_open')
sign_tk.geometry('300x200')
sign_tk.configure(bg=BG_DARK)

first_label = tk.Label(sign_tk, text="name and users key: ",
                       bg=BG_DARK, fg=FG_LIGHT)
first_label.pack()

tk_input_name = tk.Entry(sign_tk, bg=BG_ENTRY, fg=FG_LIGHT,
                         insertbackground=CURSOR_COLOR)
tk_input_name.pack(pady=10)

tk_input_key = tk.Entry(sign_tk, show='*', bg=BG_ENTRY, fg=FG_LIGHT,
                        insertbackground=CURSOR_COLOR)
tk_input_key.pack(pady=10)

first_button = tk.Button(sign_tk, text='submit', command=sign_in,
                         bg=BG_BUTTON, fg=FG_LIGHT,
                         activebackground=BG_BUTTON_ACT, activeforeground=FG_LIGHT)
first_button.pack(pady=10)

sign_tk.mainloop()
