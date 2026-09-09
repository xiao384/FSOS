# ============================================================
# system_os.py — 系统操作模块（PySide6 版，已由 tkinter 迁移）
# 包含: SYSTEM_OS 类
#   - message / help       : 系统信息与帮助
#   - turn_off_system      : 关机(root)
#   - open_file            : 打开并编辑文件
#   - users 系列           : 用户管理
#   - sudo_permission      : sudo 提权
#   - auto_output          : 自动输出
#   - unzip / install      : 解压与安装（新增）
# ============================================================
import json as _json
import os
import platform
import random
import shutil
import subprocess
import time

from PySide6.QtWidgets import (QDialog, QWidget, QVBoxLayout, QHBoxLayout,
                               QListWidget, QMenuBar, QMenu,
                               QMessageBox, QFileDialog, QButtonGroup)
try:
    from PySide6.QtGui import QAction
except ImportError:  # 兼容旧版 PySide6（QAction 在 QtWidgets 中）
    from PySide6.QtWidgets import QAction

import config
import theme
from theme import window as _theme_window


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
            'unzip <包.zip> [目录]: 解压 zip 文件',
            'install <包.zip> [目录]: 解压并安装到 installed/ 目录',
            'cat <文件>: 查看文件内容',
            'write <文件> <内容>: 写入文件(root)',
            'whoami: 显示当前用户',
            'date: 显示当前日期时间',
            'cp <源> <目标>: 复制文件/目录(root)',
            'mv <源> <目标>: 移动/重命名(root)',
            'port 01: 系统控制端口(root)',
            'port 01 status/list/exec/software/register: 端口子命令',
            'register <名称> <命令>: 注册终端可调用软件(root)',
            'software list/run <名称>: 查看/运行已注册软件',
            'history: 显示历史命令',
            'clear: 清空终端(Ctrl+L)',
            'exit / logout: 退出登录',
            '提示: ↑/↓ 回溯历史, Tab 补全命令, Ctrl+C 取消当前行',
            '提示: 直接输入已注册软件名(如 vmare)即可调用',
        ]
        return '\n'.join(cmd_list)

    # 重启终端
    @property
    def reboot_bash(self):
        from ui import reboot
        return reboot()

    # 退出登录（返回登录界面）
    def logout(self):
        from ui import logout as _logout
        _logout()

    # 清空终端
    def clear(self):
        """清空终端输出区"""
        text_widget = config.return_text_tk
        if text_widget is None:
            return '终端尚未初始化'
        text_widget.clear()
        return '终端已清空'

    def time(self):
        formatted_time = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
        return f'现在时间是:{formatted_time}'

    def echo(self, text):
        """输出给定文本"""
        return text

    def history(self):
        from commands import history
        return history()

    def ls(self):
        a = os.listdir(config.cmd_path)
        return f'工作路径: {config.cmd_path}\n' + '目录下的文件和文件夹:\n' + '\n'.join(a)

    def pwd(self):
        return config.cmd_path

    # ==================== rm：安全删除 ====================
    def _resolve_path(self, raw):
        raw = (raw or '').strip()
        if not raw:
            return ''
        return os.path.abspath(os.path.join(config.cmd_path, raw))

    def _is_dangerous_path(self, target):
        target = os.path.normcase(os.path.abspath(target))
        run_path = os.path.normcase(os.path.abspath(config.__run_path__))

        if os.path.dirname(target) == target:
            return True
        if target == run_path or run_path.startswith(target + os.sep):
            return True

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
        trash_dir = os.path.join(config.__run_path__, '.trash')
        os.makedirs(trash_dir, exist_ok=True)
        name = os.path.basename(os.path.normpath(target)) or 'root'
        stamp = time.strftime('%Y%m%d_%H%M%S')
        dest = os.path.join(trash_dir, f'{name}@{stamp}')
        if os.path.exists(dest):
            dest = os.path.join(trash_dir, f'{name}@{stamp}_{random.randint(1000, 9999)}')
        shutil.move(target, dest)

    def rm(self, path):
        if not config.root_permission:
            return '权限不足：删除操作需要 root 权限'

        raw = (path or '').strip()
        if not raw:
            return '用法: rm <文件路径> 或 rm -r <目录路径>'

        recursive = False
        target = raw
        parts = raw.split(maxsplit=1)
        if parts[0] in ('-r', '-rf', '-f'):
            if len(parts) < 2:
                return '用法: rm -r <目录路径>'
            recursive = True
            target = parts[1].strip()

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

        confirmed = [False]
        C = theme.get_theme()
        win = _theme_window(config.main_tk, '确认删除', 380, 150, C)
        layout = QVBoxLayout(win)
        kind = '目录' if is_dir else '文件'
        layout.addWidget(theme.label(win, f'确认要删除{kind} "{target}" 吗？', C))
        layout.addWidget(theme.label(win, '删除后可在回收站(.trash)中恢复', C,
                                     fg=C['error'], font=('', 8)))

        def do_delete():
            confirmed[0] = True
            win.accept()

        btn_frame = QHBoxLayout()
        btn_frame.addWidget(theme.button(win, '确认删除', do_delete, C, bg='#f44336'))
        btn_frame.addWidget(theme.button(win, '取消', win.reject, C))
        layout.addLayout(btn_frame)

        win.setModal(True)
        win.exec()
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
        system = platform.system()
        if self.users_permission_detailed == 'root':
            if system == "Windows":
                shutdown_cmd = ["shutdown", "/s", "/t", "1"]
            elif system == "Linux":
                shutdown_cmd = ["shutdown", "now"]
            elif system == "Darwin":
                shutdown_cmd = ["osascript", "-e",
                                'tell app "System Events" to shut down']
            else:
                return f"当前系统 ({system}) 暂不支持关机命令"
        else:
            return "权限不足：关机操作需要 root 权限"

        confirmed = [False]

        def finally_turn_off():
            confirmed[0] = True
            turn_off_confirm.accept()
            try:
                subprocess.run(shutdown_cmd, check=True)
            except subprocess.CalledProcessError as e:
                QMessageBox.critical(turn_off_confirm, "关机失败",
                                     f"命令执行失败: {e}\n请检查是否有足够权限。")

        def cancel_turn_off():
            turn_off_confirm.reject()

        C = theme.get_theme()
        turn_off_confirm = _theme_window(config.main_tk, '确认关机', 250, 120, C)
        layout = QVBoxLayout(turn_off_confirm)
        layout.addWidget(theme.label(turn_off_confirm, f'确认要关机吗？\n({system})', C))
        btn_frame = QHBoxLayout()
        btn_frame.addWidget(theme.button(turn_off_confirm, '确认关机', finally_turn_off, C,
                                         bg='#c42b1c', width=8))
        btn_frame.addWidget(theme.button(turn_off_confirm, '取消', cancel_turn_off, C, width=8))
        layout.addLayout(btn_frame)

        turn_off_confirm.setModal(True)
        turn_off_confirm.exec()
        if confirmed[0]:
            return '系统正在关机...'
        else:
            return '已取消关机'

    # 切换工作路径
    def cd(self, new_path):
        new_path = (new_path or '').strip()
        if not new_path:
            return f'用法: cd <目录路径>\n当前工作路径: {config.cmd_path}'
        target = os.path.abspath(os.path.join(config.cmd_path, new_path))
        if not os.path.isdir(target):
            return f'路径不存在或不是目录: {target}'
        config.cmd_path = target
        return f'工作路径已切换为: {config.cmd_path}'

    def mkdir(self, wjj_path):
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

        C = theme.get_theme()
        open_file_tk = _theme_window(config.main_tk, file_path, 500, 400, C)
        layout = QVBoxLayout(open_file_tk)

        menubar = QMenuBar(open_file_tk)
        file_menu = menubar.addMenu('文件')
        text_widget = theme.text(open_file_tk, C, font=('Consolas', 10))
        text_widget.setPlainText(content)

        def save_file():
            try:
                new_content = text_widget.toPlainText()
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                QMessageBox.information(open_file_tk, '提示', f'文件已保存: {file_path}')
            except Exception as e:
                QMessageBox.critical(open_file_tk, '保存失败', str(e))

        def close_window():
            open_file_tk.close()

        act_save = QAction('保存 (Ctrl+S)', open_file_tk)
        act_save.triggered.connect(save_file)
        file_menu.addAction(act_save)
        file_menu.addSeparator()
        act_close = QAction('关闭', open_file_tk)
        act_close.triggered.connect(close_window)
        file_menu.addAction(act_close)

        layout.setMenuBar(menubar)
        layout.addWidget(text_widget)
        open_file_tk.show()
        return f'已打开文件: {file_path}'

    # ==================== 用户管理 ====================
    def _load_users_data(self):
        with open(os.path.join(config.__run_path__, 'users_data.json'), 'r', encoding='utf-8') as f:
            return _json.load(f)

    def _save_users_data(self, data):
        with open(os.path.join(config.__run_path__, 'users_data.json'), 'w', encoding='utf-8') as f:
            _json.dump(data, f, ensure_ascii=False, indent=4)

    def users(self, args_str):
        args = args_str.split() if args_str.strip() else []

        if not args:
            return (f'当前用户: {self.users_name}\n'
                    f'权限: {self.users_permission_detailed}')

        sub_cmd = args[0]

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
        try:
            data = self._load_users_data()
        except Exception as e:
            return f'读取用户数据失败: {e}'

        if target not in data['users_name']:
            return f'用户 "{target}" 不存在'

        idx = data['users_name'].index(target)
        C = theme.get_theme()
        win = _theme_window(config.main_tk, f'修改用户: {target}', 340, 280, C)
        layout = QVBoxLayout(win)

        title_frame = QHBoxLayout()
        title_frame.addWidget(theme.label(win, '正在修改用户: ', C))
        title_frame.addWidget(theme.label(win, target, C, fg='#2196F3', font=('', 10, 'bold')))
        layout.addLayout(title_frame)

        layout.addWidget(theme.label(win, '新密码 (留空则不修改):', C))
        pwd_entry = theme.entry(win, C, show='*', width=35)
        layout.addWidget(pwd_entry)
        layout.addWidget(theme.label(win, '确认新密码:', C))
        pwd_confirm_entry = theme.entry(win, C, show='*', width=35)
        layout.addWidget(pwd_confirm_entry)

        layout.addWidget(theme.label(win, '用户权限:', C, font=('', 10)))
        perm_frame = QHBoxLayout()
        rb_root = theme.radiobutton(perm_frame, 'root  (管理员)', C)
        rb_users = theme.radiobutton(perm_frame, 'users (普通用户)', C)
        perm_group = QButtonGroup(perm_frame)
        perm_group.addButton(rb_root)
        perm_group.addButton(rb_users)
        rb_root.setProperty('value', 'root')
        rb_users.setProperty('value', 'users')
        if data['users_root'][idx] == 'root':
            rb_root.setChecked(True)
        else:
            rb_users.setChecked(True)
        perm_frame.addWidget(rb_root)
        perm_frame.addWidget(rb_users)
        layout.addLayout(perm_frame)

        layout.addWidget(theme.label(win, f'当前密码: {data["users_password"][idx]}', C,
                                     fg='#888', font=('', 8)))

        confirmed = [False]

        def do_save():
            new_pwd = pwd_entry.text()
            pwd_confirm = pwd_confirm_entry.text()
            if new_pwd:
                if new_pwd != pwd_confirm:
                    QMessageBox.critical(win, '错误', '两次输入的密码不一致')
                    return
                data['users_password'][idx] = new_pwd
            data['users_root'][idx] = rb_root.property('value') if rb_root.isChecked() else rb_users.property('value')
            try:
                self._save_users_data(data)
            except Exception as e:
                QMessageBox.critical(win, '保存失败', str(e))
                return
            if target == self.users_name:
                self.users_permission_detailed = rb_root.property('value') if rb_root.isChecked() else rb_users.property('value')
                config.root_permission = (self.users_permission_detailed == 'root')
            confirmed[0] = True
            win.accept()

        btn_frame = QHBoxLayout()
        btn_frame.addWidget(theme.button(win, '保存修改', do_save, C, bg='#4CAF50', width=12))
        btn_frame.addWidget(theme.button(win, '取消', win.reject, C, width=12))
        layout.addLayout(btn_frame)

        win.setModal(True)
        win.exec()
        if not confirmed[0]:
            return '已取消'
        return f'用户 "{target}" 修改完成'

    def _add_user_gui(self):
        C = theme.get_theme()
        win = _theme_window(config.main_tk, '添加新用户', 340, 400, C)
        layout = QVBoxLayout(win)

        layout.addWidget(theme.label(win, '创建新用户', C, font=('', 11, 'bold')))
        layout.addWidget(theme.label(win, '用户名:', C))
        name_entry = theme.entry(win, C, width=35)
        layout.addWidget(name_entry)
        layout.addWidget(theme.label(win, '密码:', C))
        pwd_entry = theme.entry(win, C, show='*', width=35)
        layout.addWidget(pwd_entry)
        layout.addWidget(theme.label(win, '确认密码:', C))
        pwd_confirm_entry = theme.entry(win, C, show='*', width=35)
        layout.addWidget(pwd_confirm_entry)

        layout.addWidget(theme.label(win, '用户权限:', C, font=('', 10)))
        perm_frame = QHBoxLayout()
        rb_root = theme.radiobutton(perm_frame, 'root  (管理员)', C)
        rb_users = theme.radiobutton(perm_frame, 'users (普通用户)', C)
        perm_group = QButtonGroup(perm_frame)
        perm_group.addButton(rb_root)
        perm_group.addButton(rb_users)
        rb_root.setProperty('value', 'root')
        rb_users.setProperty('value', 'users')
        rb_users.setChecked(True)
        perm_frame.addWidget(rb_root)
        perm_frame.addWidget(rb_users)
        layout.addLayout(perm_frame)

        confirmed = [False]

        def do_add():
            new_name = name_entry.text().strip()
            new_pwd = pwd_entry.text()
            pwd_confirm = pwd_confirm_entry.text()
            if not new_name:
                QMessageBox.critical(win, '错误', '用户名不能为空')
                return
            if not new_pwd:
                QMessageBox.critical(win, '错误', '密码不能为空')
                return
            if new_pwd != pwd_confirm:
                QMessageBox.critical(win, '错误', '两次输入的密码不一致')
                return
            try:
                data = self._load_users_data()
            except Exception as e:
                QMessageBox.critical(win, '错误', f'读取用户数据失败: {e}')
                return
            if new_name in data['users_name']:
                QMessageBox.critical(win, '错误', f'用户 "{new_name}" 已存在')
                return
            data['users_name'].append(new_name)
            data['users_password'].append(new_pwd)
            data['users_root'].append(rb_root.property('value') if rb_root.isChecked() else rb_users.property('value'))
            try:
                self._save_users_data(data)
            except Exception as e:
                QMessageBox.critical(win, '保存失败', str(e))
                return
            confirmed[0] = True
            win.accept()

        btn_frame = QHBoxLayout()
        btn_frame.addWidget(theme.button(win, '添加用户', do_add, C, bg='#4CAF50', width=12))
        btn_frame.addWidget(theme.button(win, '取消', win.reject, C, width=12))
        layout.addLayout(btn_frame)

        win.setModal(True)
        win.exec()
        if not confirmed[0]:
            return '已取消'
        return '用户添加操作已完成'

    def _delete_user(self, target):
        try:
            data = self._load_users_data()
        except Exception as e:
            return f'读取用户数据失败: {e}'

        if target not in data['users_name']:
            return f'用户 "{target}" 不存在'

        if target == self.users_name:
            return '不能删除当前登录的用户'

        idx = data['users_name'].index(target)
        confirmed = [False]
        C = theme.get_theme()
        win = _theme_window(config.main_tk, '确认删除', 300, 130, C)
        layout = QVBoxLayout(win)
        layout.addWidget(theme.label(win, f'确认要删除用户 "{target}" 吗？', C))
        layout.addWidget(theme.label(win, '此操作不可撤销！', C, fg=C['error']))

        def do_delete():
            confirmed[0] = True
            win.accept()

        btn_frame = QHBoxLayout()
        btn_frame.addWidget(theme.button(win, '确认删除', do_delete, C, bg='#f44336', width=12))
        btn_frame.addWidget(theme.button(win, '取消', win.reject, C, width=12))
        layout.addLayout(btn_frame)

        win.setModal(True)
        win.exec()
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

    # ==================== sudo 提权（在当前终端内输入密码） ====================
    def _verify_root_password(self, password):
        """校验输入的密码是否等于 root 用户密码"""
        try:
            with open(os.path.join(config.__run_path__, 'users_data.json'),
                      'r', encoding='utf-8') as f:
                data = _json.load(f)
            for i, name in enumerate(data['users_name']):
                if name == 'root':
                    return data['users_password'][i] == password
        except Exception:
            pass
        return False

    def _request_sudo(self, mode, command_text=''):
        """请求 sudo 密码验证：设置状态并返回提示语，由终端在当前界面内接收密码"""
        config.sudo_state = {
            'mode': mode,
            'command_text': command_text,
            'attempts': 0,
        }
        return '[sudo] password for root:'

    def sudo_permission(self, account):
        if account != 'root':
            return '用法: sudo root'
        if config.root_permission:
            return '当前已是 root 权限，无需再次提权'
        return self._request_sudo('root')

    def sudo_run(self, command_text):
        command_text = (command_text or '').strip()
        if not command_text:
            return '用法: sudo root 或 sudo <命令>'
        if command_text == 'root':
            return self.sudo_permission('root')
        if config.root_permission:
            from commands import run_cmd_text
            return run_cmd_text(command_text)
        if command_text.lower().startswith('sudo'):
            return '不支持在 sudo 中嵌套 sudo'
        return self._request_sudo('run', command_text)

    def _execute_sudo_run(self, command_text):
        """密码已验证通过后，临时以 root 权限执行命令"""
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

    # ==================== 端口 01：系统控制端口 ====================
    RESERVED_PORTS = {'01': 'SYSTEM_CONTROL'}

    def _ensure_root_for_port01(self):
        """01 端口系统控制必须持有 root 权限"""
        if not config.root_permission:
            return '权限不足：端口 01 系统控制需要 root 权限'
        return None

    def _manifest_path(self):
        return os.path.join(config.__run_path__, 'installed', 'manifest.json')

    def _load_manifest(self):
        path = self._manifest_path()
        if not os.path.isfile(path):
            return {}
        try:
            with open(path, 'r', encoding='utf-8') as f:
                data = _json.load(f)
            if not isinstance(data, dict):
                return {}
            return data
        except Exception:
            return {}

    def _save_manifest(self, data):
        path = self._manifest_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as f:
            _json.dump(data, f, ensure_ascii=False, indent=2)

    def port_control(self, args):
        """port 01 [status|list|exec <cmd>|software <name>|register <name> <cmd>|shutdown|reboot]"""
        parts = (args or '').strip().split(maxsplit=1)
        if not parts or parts[0] != '01':
            return ('用法: port 01 <子命令>\n'
                    '预留端口: ' + ', '.join(f'{k}={v}' for k, v in self.RESERVED_PORTS.items())
                    + '\n示例: port 01 status')

        err = self._ensure_root_for_port01()
        if err:
            return err

        sub = parts[1].strip() if len(parts) > 1 else 'status'
        sub_parts = sub.split(maxsplit=1)
        action = sub_parts[0].lower()
        action_args = sub_parts[1] if len(sub_parts) > 1 else ''

        if action == 'status':
            return (f'端口 01 ({self.RESERVED_PORTS["01"]}) 状态正常\n'
                    f'当前用户: {self.users_name}  权限: {self.users_permission_detailed}\n'
                    f'系统: {platform.platform()}')

        if action == 'list':
            manifest = self._load_manifest()
            lines = ['=== 已注册软件（端口 01 可调用的软件）===']
            if not manifest:
                lines.append('（暂无）')
            else:
                for name, info in manifest.items():
                    if isinstance(info, dict):
                        lines.append(f'  {name}: {info.get("command", "(无启动命令)")}')
                    else:
                        lines.append(f'  {name}: {info}')
            return '\n'.join(lines)

        if action == 'exec':
            if not action_args:
                return '用法: port 01 exec <系统命令>'
            return self._exec_system(action_args)

        if action == 'register':
            return self.register_software(action_args)

        if action == 'software':
            return self.software_cmd(action_args)

        if action == 'shutdown':
            return self.turn_off_system()

        if action == 'reboot':
            return self.reboot_bash()

        return (f'未知子命令: {action}\n'
                '可用: status | list | exec <cmd> | software <name> | register <name> <cmd> | shutdown | reboot')

    def _exec_system(self, cmd, background=False):
        """在真实操作系统上执行系统命令"""
        try:
            if background:
                popen_kwargs = dict(
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    stdin=subprocess.DEVNULL,
                    start_new_session=True,
                )
                if os.name == 'nt':
                    popen_kwargs['creationflags'] = (
                        subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
                    )
                subprocess.Popen(cmd, shell=True, **popen_kwargs)
                return f'已在后台执行: {cmd}'
            else:
                result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
                out = (result.stdout or '') + (result.stderr or '')
                return out.strip() or f'命令执行完成，返回码: {result.returncode}'
        except Exception as e:
            return f'执行失败: {e}'

    def register_software(self, args):
        """register <名称> <启动命令>  —— 把软件注册到清单，之后可直接输入名称调用"""
        err = self._ensure_root_for_port01()
        if err:
            return err

        parts = (args or '').strip().split(maxsplit=1)
        if len(parts) < 2:
            return '用法: register <软件名称> <启动命令>\n示例: register vmare "C:\\Program Files (x86)\\VMware\\VMware Workstation\\vmrun.exe start ..."'
        name, command = parts[0], parts[1].strip()
        manifest = self._load_manifest()
        manifest[name] = {
            'path': command,
            'command': command,
            'terminal': True,
            'registered_at': time.strftime('%Y-%m-%d %H:%M:%S'),
        }
        self._save_manifest(manifest)
        return f'已注册软件 "{name}"，命令: {command}\n提示: 在终端直接输入 "{name}" 即可调用'

    def software_cmd(self, args):
        """software list / software run <name>"""
        parts = (args or '').strip().split(maxsplit=1)
        if not parts:
            return '用法: software list  或  software run <名称>'
        sub = parts[0].lower()
        if sub == 'list':
            return self.port_control('01 list')
        if sub == 'run':
            if len(parts) < 2:
                return '用法: software run <名称>'
            return self.run_software(parts[1], '')
        return '用法: software list  或  software run <名称>'

    def run_software(self, name, args):
        """按名称启动已注册软件；未注册返回 None，供命令分发器回退"""
        name = (name or '').strip()
        if not name:
            return None
        manifest = self._load_manifest()
        info = manifest.get(name)

        # 对 "vmare" 做 VMware 自动探测（兼容常见拼写 / 路径）
        if info is None and name.lower() in ('vmare', 'vmware'):
            vmrun = self._find_vmrun()
            if vmrun:
                command = f'"{vmrun}"'
                if args:
                    command = f'{command} {args}'
                return self._exec_system(command, background=True)
            return f'未找到 VMware(vmrun)。如需使用，请执行: register {name} "<vmrun路径> start <vmx路径>"'

        if info is None:
            return None
        if isinstance(info, str):
            command = info
        else:
            if info.get('terminal') is False:
                return f'软件 "{name}" 未配置为终端可调用'
            command = info.get('command') or info.get('path', '')
        if not command:
            return f'软件 "{name}" 未配置启动命令'

        if args:
            command = f'{command} {args}'
        return self._exec_system(command, background=True)

    def _find_vmrun(self):
        """尝试定位 vmrun.exe（VMware Workstation CLI）"""
        candidates = [
            r'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe',
            r'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
        ]
        for c in candidates:
            if os.path.isfile(c):
                return c
        # PATH 中查找
        try:
            return shutil.which('vmrun')
        except Exception:
            return None

    # ==================== Auto-output ====================
    def _resolve_auto_output_file(self, raw_path):
        raw_path = raw_path.strip()
        if not raw_path:
            return None, '请输入文件路径'

        if os.path.isfile(raw_path):
            return raw_path, None
        if os.path.isdir(raw_path):
            files = sorted(f for f in os.listdir(raw_path) if os.path.isfile(os.path.join(raw_path, f)))
            if not files:
                return None, f'目录内没有可输出的文件: {raw_path}'
            selected = self._pick_file_from_dir(raw_path, files)
            return (selected, None) if selected is not None else (None, '已取消')
        if os.path.dirname(raw_path) == '':
            candidate = os.path.join(config.cmd_path, raw_path)
            if os.path.isfile(candidate):
                return candidate, None
            return None, (f'工作路径下未找到文件: {raw_path}\n'
                          f'工作路径: {config.cmd_path}')
        return None, f'文件不存在: {raw_path}'

    def _pick_file_from_dir(self, directory, files):
        path, _ = QFileDialog.getOpenFileName(config.main_tk, '选择要输出的文件', directory)
        return path or None

    def auto_output(self, choose):
        if choose not in ('1', '2'):
            return '用法: Auto-output <1(文件内容) 或 2(手动输入)>'

        result = [None]

        def start_auto_output():
            try:
                count = int(auto_output_tk_Entry2.text())
                delay = int(auto_output_tk_Entry3.text())
            except ValueError:
                QMessageBox.critical(auto_output_tk, '错误', '次数和倒计时必须为整数')
                return
            if count <= 0 or delay < 0:
                QMessageBox.critical(auto_output_tk, '错误', '次数必须 > 0，倒计时必须 >= 0')
                return

            if choose == '1':
                file_path, err = self._resolve_auto_output_file(auto_output_tk_Entry.text())
                if err:
                    QMessageBox.critical(auto_output_tk, '错误', err)
                    return
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        text = f.read()
                except Exception as e:
                    QMessageBox.critical(auto_output_tk, '错误', f'读取文件失败: {e}')
                    return
            else:
                text = auto_output_tk_Entry.text()
                if not text.strip():
                    QMessageBox.critical(auto_output_tk, '错误', '请输入要输出的内容')
                    return

            auto_output_tk.accept()
            result[0] = (text, count, delay)

        C = theme.get_theme()
        auto_output_tk = _theme_window(config.main_tk, 'Auto-output', 450, 350, C)
        layout = QVBoxLayout(auto_output_tk)

        if choose == '1':
            layout.addWidget(theme.label(auto_output_tk, '请输入文件路径、目录或文件名:', C))
            auto_output_tk_Entry = theme.entry(auto_output_tk, C, width=50)
            layout.addWidget(auto_output_tk_Entry)
        else:
            layout.addWidget(theme.label(auto_output_tk, '请输入要自动输出的内容:', C))
            auto_output_tk_Entry = theme.entry(auto_output_tk, C, width=50)
            layout.addWidget(auto_output_tk_Entry)

        layout.addWidget(theme.label(auto_output_tk, '请输入输出次数:', C))
        auto_output_tk_Entry2 = theme.entry(auto_output_tk, C, width=20)
        auto_output_tk_Entry2.setText('1')
        layout.addWidget(auto_output_tk_Entry2)
        layout.addWidget(theme.label(auto_output_tk, '请输入开始倒计时(秒):', C))
        auto_output_tk_Entry3 = theme.entry(auto_output_tk, C, width=20)
        auto_output_tk_Entry3.setText('3')
        layout.addWidget(auto_output_tk_Entry3)

        layout.addWidget(theme.button(auto_output_tk, '开始自动输出', start_auto_output, C,
                                     bg='#2196F3', width=15))
        layout.addWidget(theme.label(auto_output_tk, '提示: 点击后切换到目标输入框等待', C,
                                     fg='#888', font=('', 8)))

        auto_output_tk.setModal(True)
        auto_output_tk.exec()

        if result[0] is None:
            return '已取消'

        text, count, delay = result[0]
        for remaining in range(delay, 0, -1):
            time.sleep(1)

        # 模拟键盘粘贴（需要 pyautogui / pyperclip）
        try:
            import pyperclip
            import pyautogui
        except Exception:
            return 'auto-output 需要 pyautogui 与 pyperclip（pip install pyautogui pyperclip）'
        pyperclip.copy(text)
        for i in range(count):
            time.sleep(random.uniform(0.2, 0.5))
            pyautogui.hotkey('ctrl', 'v')
            pyautogui.press('enter')
        return f'自动输出完成，共输出 {count} 次'

    # ==================== 解压与安装（新增） ====================
    def unzip(self, args):
        import zipfile
        parts = (args or '').strip().split(maxsplit=1)
        if not parts:
            return '用法: unzip <压缩包.zip> [目标目录]'
        archive = parts[0].strip()
        dest = parts[1].strip() if len(parts) > 1 else None
        if not os.path.isfile(archive):
            return f'文件不存在: {archive}'
        if dest is None:
            dest = os.path.splitext(os.path.basename(archive))[0]
        try:
            with zipfile.ZipFile(archive, 'r') as z:
                z.extractall(dest)
            return f'已解压到: {dest}'
        except Exception as e:
            return f'解压失败: {e}'

    def install(self, args):
        import zipfile
        parts = (args or '').strip().split(maxsplit=1)
        if not parts:
            return '用法: install <压缩包.zip> [目标目录]'
        archive = parts[0].strip()
        dest = parts[1].strip() if len(parts) > 1 else None
        if not os.path.isfile(archive):
            return f'安装包不存在: {archive}'
        name = os.path.splitext(os.path.basename(archive))[0]
        if dest is None:
            dest = os.path.join(config.__run_path__, 'installed', name)
        os.makedirs(dest, exist_ok=True)
        try:
            with zipfile.ZipFile(archive, 'r') as z:
                z.extractall(dest)

            # 如果压缩包内包含 software.json，自动注册终端可调用命令
            sw_info = None
            sw_json_path = os.path.join(dest, 'software.json')
            if os.path.isfile(sw_json_path):
                try:
                    with open(sw_json_path, 'r', encoding='utf-8') as f:
                        sw_info = _json.load(f)
                except Exception:
                    sw_info = None

            data = self._load_manifest()
            entry = {'path': dest, 'terminal': True}
            if isinstance(sw_info, dict):
                entry['command'] = sw_info.get('command', '')
                entry['description'] = sw_info.get('description', '')
                entry['terminal'] = sw_info.get('terminal', True)
            else:
                entry['command'] = dest
            data[name] = entry
            self._save_manifest(data)

            extra = ''
            if entry.get('command'):
                extra = f'\n已注册终端命令 "{name}"，可直接输入名称调用'
            return f'已安装 "{name}" 到: {dest}' + extra
        except Exception as e:
            return f'安装失败: {e}'

    # ==================== 文件查看/编辑/移动（新增） ====================
    def cat(self, args):
        """打印文件内容到终端"""
        args = (args or '').strip()
        if not args:
            return '用法: cat <文件路径>'
        target = self._resolve_path(args)
        if not os.path.isfile(target):
            return f'文件不存在: {target}'
        try:
            with open(target, 'r', encoding='utf-8') as f:
                return f.read()
        except Exception as e:
            return f'读取失败: {e}'

    def write(self, args):
        if not config.root_permission:
            return '权限不足：写文件需要 root 权限'
        parts = (args or '').strip().split(maxsplit=1)
        if len(parts) < 2:
            return '用法: write <文件路径> <内容>'
        target = self._resolve_path(parts[0])
        if self._is_dangerous_path(target):
            return f'拒绝写入：{target} 是受保护的系统路径'
        try:
            parent = os.path.dirname(target)
            if parent:
                os.makedirs(parent, exist_ok=True)
            with open(target, 'w', encoding='utf-8') as f:
                f.write(parts[1])
            return f'已写入: {target}'
        except Exception as e:
            return f'写入失败: {e}'

    def whoami(self):
        return f'{self.users_name} (权限: {self.users_permission_detailed})'

    def date(self):
        return self.time()

    def cp(self, args):
        if not config.root_permission:
            return '权限不足：复制需要 root 权限'
        parts = (args or '').strip().split()
        if len(parts) < 2:
            return '用法: cp <源> <目标>'
        src = self._resolve_path(parts[0])
        dst = self._resolve_path(parts[1])
        if not os.path.exists(src):
            return f'源不存在: {src}'
        try:
            if os.path.isdir(src):
                shutil.copytree(src, dst)
            else:
                shutil.copy2(src, dst)
            return f'已复制: {src} -> {dst}'
        except Exception as e:
            return f'复制失败: {e}'

    def mv(self, args):
        if not config.root_permission:
            return '权限不足：移动需要 root 权限'
        parts = (args or '').strip().split()
        if len(parts) < 2:
            return '用法: mv <源> <目标>'
        src = self._resolve_path(parts[0])
        dst = self._resolve_path(parts[1])
        if not os.path.exists(src):
            return f'源不存在: {src}'
        try:
            shutil.move(src, dst)
            return f'已移动: {src} -> {dst}'
        except Exception as e:
            return f'移动失败: {e}'
