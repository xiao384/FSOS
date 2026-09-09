# ============================================================
# commands.py — 命令解析与分发模块（PySide6 版）
# 将终端输入的命令字符串解析并分发给 SYSTEM_OS 对应方法
# ============================================================
import config

history_cmd = []

# 所有可识别的命令名（供分发与 Tab 补全共用）
COMMAND_NAMES = ['turn_off_system', 'message', 'help', 'echo', 'open', 'users',
                 'auto-output', 'clear', 'time', 'history', 'ls', 'cd', 'pwd',
                 'mkdir', 'rm', 'unzip', 'install', 'cat', 'write', 'whoami',
                 'date', 'cp', 'mv', 'exit', 'logout', 'port', 'register',
                 'software']


def run_cmd_text(text_input):
    # 多词命令（含空格）先整体匹配，避免被按空格拆散
    if text_input == 'reboot bash':
        return config.root.reboot_bash

    # 分割命令和参数（如 "open test.txt" → cmd="open", args="test.txt"）
    parts = text_input.split(maxsplit=1)

    # sudo 前缀：临时以 root 权限执行后续命令
    if parts and parts[0].lower() == 'sudo':
        history_cmd.append('sudo')
        return config.root.sudo_run(parts[1].strip() if len(parts) > 1 else '')

    cmd = parts[0].lower()
    args = parts[1] if len(parts) > 1 else ''
    agree_run = COMMAND_NAMES
    if cmd in agree_run:
        history_cmd.append(cmd)
        if cmd == 'help':
            return config.root.help
        elif cmd == 'turn_off_system':
            return config.root.turn_off_system
        elif cmd == 'message':
            return config.root.message
        elif cmd == 'echo':
            return config.root.echo(args)
        elif cmd == 'open':
            if not args:
                return '用法: open <文件路径>'
            return config.root.open_file(args)
        elif cmd == 'users':
            return config.root.users(args)
        elif cmd == 'auto-output':
            return config.root.auto_output(args)
        elif cmd == 'clear':
            return config.root.clear()
        elif cmd == 'time':
            return config.root.time()
        elif cmd == 'history':
            return config.root.history()
        elif cmd == 'ls':
            return config.root.ls()
        elif cmd == 'cd':
            return config.root.cd(args)
        elif cmd == 'pwd':
            return config.root.pwd()
        elif cmd == 'mkdir':
            return config.root.mkdir(args)
        elif cmd == 'rm':
            return config.root.rm(args)
        elif cmd == 'unzip':
            return config.root.unzip(args)
        elif cmd == 'install':
            return config.root.install(args)
        elif cmd == 'cat':
            if not args:
                return '用法: cat <文件路径>'
            return config.root.cat(args)
        elif cmd == 'write':
            return config.root.write(args)
        elif cmd == 'whoami':
            return config.root.whoami()
        elif cmd == 'date':
            return config.root.date()
        elif cmd == 'cp':
            return config.root.cp(args)
        elif cmd == 'mv':
            return config.root.mv(args)
        elif cmd in ('exit', 'logout'):
            return config.root.logout()
        elif cmd == 'port':
            return config.root.port_control(args)
        elif cmd == 'register':
            return config.root.register_software(args)
        elif cmd == 'software':
            return config.root.software_cmd(args)
        return f'命令 "{cmd}" 暂未实现'
    else:
        # 未识别命令：尝试匹配已注册软件名（如 vmare）
        result = config.root.run_software(cmd, args)
        if result is not None:
            return result
        return ('错误，未存在命令\n'
                '你可以输入help来查询可用命令')


def history():
    if not config.cmd_history:
        return '暂无历史命令'
    lines = [f'  {i+1}  {c}' for i, c in enumerate(config.cmd_history)]
    return '历史命令:\n' + '\n'.join(lines)
