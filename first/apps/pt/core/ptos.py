# ============================================================
# ptos.py - 系统操作核心 (由 _upstream/system_os.py 改造)
#
# 改造要点:
#   1) 所有 tk 对话框 (tk.Toplevel / wait_window / messagebox) 改为调用
#      core/backend.py 的后端接口, 于是核心不再依赖任何 GUI 工具包;
#   2) 所有文件操作改为经 self.fs (HostFS / KrnFS), 不再直接 os 调用;
#   3) 用户管理在内核走 krn.*, 在宿主机走 users_data.json;
#   4) 新增解压/安装相关命令: unzip / install / pkg / run。
#
# 内核兼容: 无 f-string / % / str.format; os、json、platform、subprocess、
#   time 等均以 try/except ImportError 探测, 缺失时给出明确降级提示。
# ============================================================
from core.config import config
from core.backend import get_backend
# 注意: 下面两行在 tools/bundle_pt.py 拼合单文件时会被剔除, 因为届时
# run_cmd_text / cmd_history 已是同一文件里的全局名。因此只能"导入名字",
# 不能"导入模块后再取属性" —— 后者在拼合后会找不到模块。
from core.commands import run_cmd_text, cmd_history
from pkg.ptpkg import (install_zip, uninstall, list_packages, app_source,
                       get_package_bytes, store_package, as_text,
                       MAX_FILE_BYTES)
from pkg.tinyunzip import read_zip

# ---- 可选依赖 (内核里均不可用) ----
try:
    import os
except ImportError:
    os = None
try:
    import json
except ImportError:
    json = None
try:
    import platform
except ImportError:
    platform = None
try:
    import subprocess
except ImportError:
    subprocess = None
try:
    import time
except ImportError:
    time = None


# 帮助文本抽成模块级常量: tools/bundle_pt.py 在拼合内核版时, 会于文件末尾
# 用 kernel/help_ascii.py 的同名常量覆盖它 —— 因为内核屏幕是 8x8 字模的
# ASCII 终端, 中文会渲染成乱码, 而宿主机 PySide6 窗口需要中文版。
HELP_LINES = [
    'help                        帮助界面',
    'message                     系统信息',
    'turn_off_system             关机 (root)',
    'open <路径>                  打开并编辑文件',
    'users                       查看当前用户信息',
    'users list                  列出所有用户',
    'users modify <用户名>        修改用户密码/权限 (root)',
    'users add                   添加新用户 (root)',
    'users delete <用户名>        删除用户 (root)',
    'sudo root                   获得 root 权限 (永久)',
    'sudo <命令>                  以 root 权限临时执行一条命令',
    'auto-output <1|2>           自动输出 (1=文件内容, 2=手动输入)',
    'reboot bash                 重启终端 (需重新登录)',
    'clear                       清空终端',
    'time                        当前时间',
    'echo <文本>                  输出文本',
    'history                     历史命令',
    'ls                          列出当前文件',
    'pwd                         当前工作路径',
    'cd <路径>                    切换工作路径 (宿主机)',
    'mkdir <路径>                 创建目录 (宿主机)',
    'rm <文件>                    删除文件 (root, 移入回收站)',
    '---- 包管理 ----',
    'pkg list                    列出内嵌包与已安装包',
    'pkg remove <包名>            卸载已安装的包',
    'unzip <包名>                 把包解压到文件区 (不登记索引)',
    'unzip -l <包名>              列出包内文件',
    'install <包名>               解压并登记为已安装应用',
    'run <包名|文件>              运行应用、脚本或可执行文件',
    'run <.py>                    直接运行 Python 脚本',
    'run <.sh>                    按 FSOS Shell 命令逐行执行脚本',
    'run <.elf>                   运行 x86-64 Linux PIE ELF',
]


class SYSTEM_OS(object):
    def __init__(self, system_name, users_name, users_permission_detailed,
                 fs=None):
        self.system_name = system_name
        self.users_name = users_name
        self.users_permission_detailed = users_permission_detailed
        self.fs = fs if fs is not None else config.fs

    # ==================== 信息 / 帮助 ====================
    @property
    def message(self):
        if config.krn is not None:
            return config.krn.message()
        if platform is None:
            return ('系统: 未知环境\n用户: ' + self.users_name +
                    '\n权限: ' + self.users_permission_detailed)
        cpu = platform.processor() or platform.machine()
        return ('系统: ' + platform.platform() + '\nCPU: ' + platform.machine() +
                '--' + cpu + '\n用户: ' + self.users_name +
                '\n权限: ' + self.users_permission_detailed)

    @property
    def help(self):
        return '\n'.join(HELP_LINES)

    # ==================== 终端控制 ====================
    @property
    def reboot_bash(self):
        return get_backend().reboot_app()

    def clear(self):
        get_backend().clear()
        return '终端已清空'

    def time(self):
        if time is None:
            return '当前环境不提供时间接口'
        return '现在时间是: ' + time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())

    def echo(self, text):
        return text

    def history(self):
        return cmd_history()

    # ==================== 内核信息 / 控制 ====================
    def whoami(self):
        """当前身份; 内核下直接问 krn"""
        if config.krn is not None:
            try:
                return str(config.krn.whoami())
            except Exception:
                pass
        return (self.users_name + '  (权限: ' +
                self.users_permission_detailed + ')')

    def drivers(self):
        """列出内核已注册驱动 (内核专有; 宿主机无此概念)"""
        if config.krn is None:
            return '当前环境不是 FSOS 内核, 没有驱动列表'
        try:
            rows = config.krn.drivers()
        except Exception as e:
            return '读取驱动列表失败: ' + str(e)
        lines = ['=== 驱动列表 ===']
        for row in rows:
            lines.append('  ' + str(row))
        if len(lines) == 1:
            lines.append('  (无)')
        return '\n'.join(lines)

    def reboot(self):
        """重启系统 (内核专有, 调用后不返回)"""
        if config.krn is None:
            return '当前环境不是 FSOS 内核, 无法重启系统'
        be = get_backend()
        if not be.confirm('确认重启', '确认要重启系统吗?', danger=True):
            return '已取消重启'
        config.krn.reboot()
        return '系统正在重启...'

    def hostname(self, new_name):
        """查看 / 设置主机名"""
        if config.krn is None:
            return '当前环境不支持主机名设置'
        new_name = (new_name or '').strip()
        try:
            if not new_name:
                return '主机名: ' + str(config.krn.hostname(''))
            return str(config.krn.hostname(new_name))
        except Exception as e:
            return '主机名操作失败: ' + str(e)

    def cat(self, path):
        """查看文件内容 (比 open 更适合快速查看)"""
        err = self._need_fs()
        if err:
            return err
        path = (path or '').strip()
        if not path:
            return '用法: cat <文件名>'
        try:
            if not self.fs.exists(path):
                return '文件不存在: ' + path
            return self.fs.read(path)
        except Exception as e:
            return '读取失败: ' + str(e)

    # ==================== 文件区 ====================
    def _need_fs(self):
        if self.fs is None:
            return '文件区未初始化'
        return None

    def ls(self):
        err = self._need_fs()
        if err:
            return err
        names = self.fs.names()
        if not names:
            return '工作路径: ' + self.fs.cwd() + '\n(空)'
        return '工作路径: ' + self.fs.cwd() + '\n' + '\n'.join(names)

    def pwd(self):
        err = self._need_fs()
        if err:
            return err
        return self.fs.cwd()

    def cd(self, new_path):
        err = self._need_fs()
        if err:
            return err
        new_path = (new_path or '').strip()
        if not new_path:
            return '用法: cd <目录路径>\n当前工作路径: ' + self.fs.cwd()
        return self.fs.chdir(new_path)

    def mkdir(self, path):
        err = self._need_fs()
        if err:
            return err
        path = (path or '').strip()
        if not path:
            return '用法: mkdir <目录名>'
        return self.fs.mkdir(path)

    def rm(self, path):
        if not config.root_permission:
            return '权限不足: 删除操作需要 root 权限'
        raw = (path or '').strip()
        if not raw:
            return '用法: rm <文件路径>'
        err = self._need_fs()
        if err:
            return err
        try:
            if self.fs.isdir(raw):
                return raw + ' 是目录, 当前文件区不支持递归删除'
        except Exception:
            pass
        be = get_backend()
        if not be.confirm('确认删除', '确认要删除 "' + raw + '" 吗?', danger=True):
            return '已取消删除'
        return self.fs.delete(raw)

    def open_file(self, file_path):
        err = self._need_fs()
        if err:
            return err
        if not self.fs.exists(file_path):
            return '文件不存在: ' + file_path
        try:
            content = self.fs.read(file_path)
        except Exception as e:
            return '读取文件失败: ' + str(e)
        new = get_backend().edit_text('编辑: ' + file_path, content)
        if new is None:
            return '已取消编辑'
        try:
            self.fs.write(file_path, new)
        except Exception as e:
            return '保存失败: ' + str(e)
        return '已保存: ' + file_path

    # ==================== 关机 / 重启 ====================
    @property
    def turn_off_system(self):
        if self.users_permission_detailed != 'root':
            return '权限不足: 关机操作需要 root 权限'
        be = get_backend()
        if not be.confirm('确认关机', '确认要关机吗?', danger=True):
            return '已取消关机'
        if config.krn is not None:
            config.krn.poweroff()
            return '系统正在关机...'
        if subprocess is None or platform is None:
            return '当前环境不支持关机命令'
        system = platform.system()
        if system == 'Windows':
            cmd = ['shutdown', '/s', '/t', '1']
        elif system == 'Linux':
            cmd = ['shutdown', 'now']
        elif system == 'Darwin':
            cmd = ['osascript', '-e', 'tell app "System Events" to shut down']
        else:
            return '当前系统 (' + system + ') 暂不支持关机命令'
        try:
            subprocess.run(cmd, check=True)
        except Exception as e:
            return '关机失败: ' + str(e)
        return '系统正在关机...'

    # ==================== 用户管理 ====================
    def _load_users(self):
        path = os.path.join(config.run_path, 'users_data.json')
        with open(path, 'r', encoding='utf-8') as f:
            return json.load(f)

    def _save_users(self, data):
        path = os.path.join(config.run_path, 'users_data.json')
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=4)

    def _list_users(self):
        if config.krn is not None:
            rows = config.krn.users()
            lines = ['=== 用户列表 ===']
            for row in rows:
                lines.append('  ' + str(row[0]) + '  权限: ' + str(row[1]))
            return '\n'.join(lines)
        if json is None or os is None:
            return '当前环境无法读取用户数据'
        try:
            data = self._load_users()
        except Exception as e:
            return '读取用户数据失败: ' + str(e)
        lines = ['=== 用户列表 ===']
        for i in range(len(data['users_name'])):
            marker = ''
            if data['users_name'][i] == self.users_name:
                marker = ' <- 当前'
            lines.append('  ' + data['users_name'][i] + '  权限: ' +
                         data['users_root'][i] + marker)
        return '\n'.join(lines)

    def _add_user(self):
        be = get_backend()
        fields = [('name', '用户名', False),
                  ('pwd', '密码', True),
                  ('pwd2', '确认密码', True)]
        got = be.ask_fields('添加新用户', fields)
        if got is None:
            return '已取消'
        name = (got.get('name') or '').strip()
        pwd = got.get('pwd') or ''
        if not name:
            return '用户名不能为空'
        if not pwd:
            return '密码不能为空'
        if pwd != got.get('pwd2'):
            return '两次输入的密码不一致'
        if config.krn is not None:
            return str(config.krn.user_add(name, pwd, 'users'))
        if json is None or os is None:
            return '当前环境无法写入用户数据'
        data = self._load_users()
        if name in data['users_name']:
            return '用户 "' + name + '" 已存在'
        data['users_name'].append(name)
        data['users_password'].append(pwd)
        data['users_root'].append('users')
        self._save_users(data)
        return '用户 "' + name + '" 已创建'

    def _modify_user(self, target):
        be = get_backend()
        fields = [('pwd', '新密码 (留空则不修改)', True),
                  ('pwd2', '确认新密码', True),
                  ('role', '权限 (root / users)', False)]
        got = be.ask_fields('修改用户: ' + target, fields)
        if got is None:
            return '已取消'
        pwd = got.get('pwd') or ''
        if pwd and pwd != got.get('pwd2'):
            return '两次输入的密码不一致'
        role = (got.get('role') or '').strip() or 'users'
        if config.krn is not None:
            out = ''
            if pwd:
                out = out + str(config.krn.user_setpass(target, pwd)) + '\n'
            return out + str(config.krn.user_setrole(target, role))
        if json is None or os is None:
            return '当前环境无法写入用户数据'
        data = self._load_users()
        if target not in data['users_name']:
            return '用户 "' + target + '" 不存在'
        idx = data['users_name'].index(target)
        if pwd:
            data['users_password'][idx] = pwd
        data['users_root'][idx] = role
        self._save_users(data)
        return '用户 "' + target + '" 已更新'

    def _delete_user(self, target):
        if target == self.users_name:
            return '不能删除当前登录的用户'
        be = get_backend()
        if not be.confirm('确认删除', '确认要删除用户 "' + target + '" 吗? 此操作不可撤销',
                          danger=True):
            return '已取消删除'
        if config.krn is not None:
            return str(config.krn.user_del(target))
        if json is None or os is None:
            return '当前环境无法写入用户数据'
        data = self._load_users()
        if target not in data['users_name']:
            return '用户 "' + target + '" 不存在'
        idx = data['users_name'].index(target)
        del data['users_name'][idx]
        del data['users_password'][idx]
        del data['users_root'][idx]
        self._save_users(data)
        return '用户 "' + target + '" 已删除'

    def users(self, args_str):
        args = args_str.split() if args_str.strip() else []
        if not args:
            return ('当前用户: ' + self.users_name +
                    '\n权限: ' + self.users_permission_detailed)
        sub = args[0]
        if sub == 'list':
            return self._list_users()
        if not config.root_permission:
            return '权限不足: 修改用户信息需要 root 权限'
        if sub == 'modify':
            if len(args) < 2:
                return '用法: users modify <用户名>'
            return self._modify_user(args[1])
        if sub == 'add':
            return self._add_user()
        if sub == 'delete':
            if len(args) < 2:
                return '用法: users delete <用户名>'
            return self._delete_user(args[1])
        return ('未知子命令: ' + sub +
                '\n可用: list | modify <用户名> | add | delete <用户名>')

    # ==================== sudo 提权 ====================
    def _ask_root_password(self):
        if config.krn is not None:
            return True      # 内核终端本身即 root, 无需口令
        if json is None or os is None:
            return False
        try:
            data = self._load_users()
        except Exception:
            return False
        names = data['users_name']
        pwds = data['users_password']
        root_pwd = None
        for i in range(len(names)):
            if names[i] == 'root':
                root_pwd = pwds[i]
                break
        if root_pwd is None:
            return False
        got = get_backend().ask_text('sudo 提权', '请输入 root 密码', secret=True)
        return got is not None and got == root_pwd

    def sudo_permission(self, account):
        if account != 'root':
            return '用法: sudo root'
        if config.root_permission:
            return '当前已是 root 权限, 无需再次提权'
        if not self._ask_root_password():
            return '已取消'
        config.root_permission = True
        self.users_permission_detailed = 'root'
        get_backend().set_title('bash-' + config.system_name + '--' +
                                self.users_name + '--root')
        return '成功获得 root 权限'

    def sudo_run(self, command_text):
        command_text = (command_text or '').strip()
        if not command_text:
            return '用法: sudo root 或 sudo <命令>'
        if command_text == 'root':
            return self.sudo_permission('root')
        if config.root_permission:
            return run_cmd_text(command_text)
        if command_text.lower().startswith('sudo'):
            return '不支持在 sudo 中嵌套 sudo'
        if not self._ask_root_password():
            return '已取消'
        old_root = config.root_permission
        old_role = self.users_permission_detailed
        config.root_permission = True
        self.users_permission_detailed = 'root'
        try:
            return run_cmd_text(command_text)
        finally:
            config.root_permission = old_root
            self.users_permission_detailed = old_role

    # ==================== 自动输出 ====================
    def auto_output(self, choose):
        if choose not in ('1', '2'):
            return '用法: auto-output <1(文件内容) 或 2(手动输入)>'
        be = get_backend()
        err = self._need_fs()
        if err:
            return err
        if choose == '1':
            path = be.ask_text('Auto-output', '文件路径')
            if not path:
                return '已取消'
            if not self.fs.exists(path):
                return '文件不存在: ' + path
            try:
                text = self.fs.read(path)
            except Exception as e:
                return '读取文件失败: ' + str(e)
        else:
            text = be.ask_text('Auto-output', '要自动输出的内容')
            if not text:
                return '已取消'
        raw_count = be.ask_text('Auto-output', '输出次数')
        raw_delay = be.ask_text('Auto-output', '倒计时秒数')
        try:
            count = int(raw_count)
            delay = int(raw_delay)
        except (TypeError, ValueError):
            return '次数与倒计时必须为整数'
        if count <= 0 or delay < 0:
            return '次数必须 > 0, 倒计时必须 >= 0'
        return be.paste(text, count, delay)

    # ==================== 包管理 ====================
    def _pkg_bytes(self, name):
        err = self._need_fs()
        if err:
            return None, err
        try:
            raw = get_package_bytes(self.fs, name)
        except Exception as e:
            return None, '读取包失败: ' + str(e)
        if raw is None:
            return None, '找不到包: ' + name + ' (用 pkg list 查看可用包)'
        return raw, None

    def unzip(self, args):
        parts = (args or '').split()
        if not parts or (len(parts) == 1 and parts[0] == '-l'):
            return '用法: unzip <包名> 或 unzip -l <包名>'
        list_only = False
        if parts[0] == '-l':
            list_only = True
            parts = parts[1:]
            if not parts:
                return '用法: unzip -l <包名>'
        name = parts[0]
        raw, err = self._pkg_bytes(name)
        if err:
            return err
        try:
            entries = read_zip(raw)
        except Exception as e:
            return '解压失败: ' + str(e)
        entries = [(n, c) for (n, c) in entries if not n.endswith('/')]
        if list_only:
            lines = ['=== ' + name + ' 包含 ' + str(len(entries)) + ' 个文件 ===']
            for fname, content in entries:
                lines.append('  ' + fname + '  (' + str(len(content)) + ' 字节)')
            return '\n'.join(lines)

        written = []
        try:
            for fname, content in entries:
                if len(fname) > 24:
                    raise ValueError('文件名过长 (上限 24): ' + fname)
                if len(content) > MAX_FILE_BYTES:
                    raise ValueError('文件过大 (上限 ' + str(MAX_FILE_BYTES) +
                                     ' 字节): ' + fname)
                self.fs.write(fname, as_text(content))
                written.append(fname)
        except Exception as e:
            return '解压失败: ' + str(e)
        return '已解压 ' + name + ': ' + ', '.join(written)

    def install(self, args):
        parts = (args or '').split()
        if not parts:
            return '用法: install <包名>'
        name = parts[0]
        raw, err = self._pkg_bytes(name)
        if err:
            return err
        try:
            return install_zip(self.fs, name, raw)
        except Exception as e:
            return '安装失败: ' + str(e)

    def pkg(self, args):
        err = self._need_fs()
        if err:
            return err
        parts = (args or '').split()
        if not parts or parts[0].lower() == 'list':
            return list_packages(self.fs)
        sub = parts[0].lower()
        if sub in ('remove', 'uninstall'):
            if len(parts) < 2:
                return '用法: pkg remove <包名>'
            return uninstall(self.fs, parts[1])
        if sub == 'export':
            # 把内嵌包导出为文件区分块, 便于二次分发
            if len(parts) < 2:
                return '用法: pkg export <包名>'
            name = parts[1]
            raw, err = self._pkg_bytes(name)
            if err:
                return err
            try:
                n = store_package(self.fs, name, raw)
            except Exception as e:
                return '导出失败: ' + str(e)
            return '已导出 ' + name + ' 为 ' + str(n) + ' 个分块'
        return ('未知子命令: ' + sub +
                '\n可用: list | remove <包名> | export <包名>')

    def _run_shell_script(self, source, filename):
        # FSOS .sh 不是 GNU bash；它是一种轻量、可移植的“命令脚本”格式。
        # 每个有效行交给同一套 run_cmd_text()，因此脚本与交互终端使用完全相同的命令实现。
        outputs = []
        lines = source.replace('\r\n', '\n').replace('\r', '\n').split('\n')
        stopped = False
        for lineno, raw in enumerate(lines, 1):
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            if line in ('set -e', 'set +e', 'set -u', 'set +u', 'set -eu', 'set -ue'):
                continue
            if line.lower() in ('@echo off', 'echo off'):
                continue
            # 去掉 shebang 已由 # 注释规则处理；支持简单的行尾注释。
            if ' #' in line:
                line = line.split(' #', 1)[0].rstrip()
            if not line:
                continue
            out = run_cmd_text(line)
            if out:
                outputs.append('[%s:%d] %s' % (filename, lineno, out))
            low = (out or '').lower()
            if low.startswith('错误') or low.startswith('运行失败') or low.startswith('未知命令'):
                stopped = True
                outputs.append('脚本已停止: 第 %d 行执行失败' % lineno)
                break
        if stopped:
            return '\n'.join(outputs)
        return '\n'.join(outputs)

    def _run_direct_file(self, name):
        # 文件名在 FSOS 内核文件区是扁平命名空间；宿主机允许相对路径。
        if name.startswith('./'):
            name = name[2:]
        if not self.fs.exists(name):
            return '文件不存在: ' + name

        lower = name.lower()
        # Linux PIE ELF：交给内核 Linuxulator；仅内核环境支持。
        if lower.endswith('.elf'):
            if config.krn is None:
                return '当前环境不支持直接运行 ELF: ' + name
            try:
                config.krn.run('run ' + name)
                return 'ELF 启动请求已提交: ' + name
            except Exception as e:
                return 'ELF 启动失败: ' + str(e)

        try:
            src = self.fs.read(name)
        except Exception as e:
            return '读取失败: ' + str(e)

        # 直接运行脚本时，允许 shebang 覆盖扩展名。
        first = src.split('\n', 1)[0].strip().lower() if src else ''
        is_shell = lower.endswith(('.sh', '.bash', '.command', '.cmd', '.bat')) or first.startswith('#!') and ('sh' in first or 'shell' in first)
        if is_shell:
            return self._run_shell_script(src, name)

        ns = {'__name__': '__main__', '__file__': name}
        if config.krn is not None:
            ns['krn'] = config.krn
        if lower.endswith('.py'):
            try:
                exec(src, ns)
            except SystemExit:
                return ''
            except Exception as e:
                return 'Python 运行失败 [%s]: %s' % (name, str(e))
            return 'Python 脚本执行完成: ' + name

        # C/Java 源文件由内核已有语言模块执行；这里必须通过 krn.run 进入统一的 C 端
        # 语言调度，而不是把源代码交给 Python exec。
        if lower.endswith(('.c', '.cc', '.cpp', '.cxx')) or lower.endswith('.java'):
            if config.krn is None:
                return '当前宿主机模式不能直接运行 FSOS C/Java 模块: ' + name
            try:
                config.krn.run('run ' + name)
                return ''
            except Exception as e:
                return '程序启动失败: ' + str(e)

        return '无法识别的可执行类型: ' + name + ' (支持 .py/.sh/.bash/.cmd/.bat/.elf/.c/.cpp/.java)'

    def run(self, args):
        parts = (args or '').split()
        if not parts:
            return '用法: run <包名|文件>'
        name = parts[0]

        # 兼容原有包管理语义：run hello 仍然优先运行已安装的 hello 入口。
        src = app_source(self.fs, name)
        if src is not None:
            ns = {'__name__': '__main__', '__file__': name}
            if config.krn is not None:
                ns['krn'] = config.krn
            try:
                exec(src, ns)
            except SystemExit:
                return ''
            except Exception as e:
                return '应用运行失败 [%s]: %s' % (name, str(e))
            return '应用执行完成: ' + name

        return self._run_direct_file(name)
