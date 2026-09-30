# ============================================================
# commands.py - 命令解析与分发
# 由 _upstream/commands.py 改造: 保留原有命令集合, 新增包管理命令,
# 并把 auto-output 的别名统一为小写 (原实现只认 Auto-output)。
# ============================================================
from core.config import config

history_cmd = []

# 可执行的命令
# 说明: reboot / drivers / whoami / hostname / cat 为系统终端补齐的命令,
#       把 krn 桥已暴露的内核能力接进 Better terminal (宿主机下会给出降级提示)。
AGREE_RUN = [
    'help', 'message', 'turn_off_system', 'echo', 'open', 'users',
    'auto-output', 'clear', 'cls', 'time', 'history', 'ls', 'cd', 'pwd',
    'mkdir', 'rm', 'unzip', 'install', 'pkg', 'run', 'theme',
    'reboot', 'drivers', 'whoami', 'hostname', 'cat',
]

# 必须有参数的命令
NEED_ARGS = ['open', 'cd', 'mkdir', 'rm', 'echo', 'auto-output',
             'unzip', 'install', 'run', 'theme', 'cat']

_USAGE = {
    'open': '用法: open <文件路径>',
    'cd': '用法: cd <目录路径>',
    'mkdir': '用法: mkdir <目录名>',
    'rm': '用法: rm <文件路径>',
    'echo': '用法: echo <文本>',
    'auto-output': '用法: auto-output <1(文件内容) 或 2(手动输入)>',
    'unzip': '用法: unzip <包名> 或 unzip -l <包名>',
    'install': '用法: install <包名>',
    'run': '用法: run <包名|文件>  (支持 .py/.sh/.bash/.cmd/.bat/.elf/.c/.cpp/.java)',
    'theme': '用法: theme <dark|light>',
}


def run_cmd_text(text_input):
    # lstrip('\xef\xbb\xbf'): 从文件/管道喂入命令时常带 UTF-8 BOM, 不去掉会让
    # "第一条命令" 被当成未知命令 (内核键盘输入不会产生 BOM, 但脚本会)。
    # 注意: 不能用 '\ufeff' -- 内嵌 MP 为 ROM_LEVEL_MINIMUM, STR_UNICODE=0,
    #       '\uXXXX' 且值 >=0x100 的词法转义直接 SyntaxError。改用 BOM 的
    #       三个 UTF-8 字节 (\xef\xbb\xbf) 表示, 语义相同。
    text_input = (text_input or '').strip().lstrip('\xef\xbb\xbf').strip()

    # 多词命令 (含空格) 先整体匹配, 避免被按空格拆散
    if text_input == 'reboot bash':
        history_cmd.append('reboot bash')
        return config.root.reboot_bash

    if not text_input:
        return ''

    parts = text_input.split(maxsplit=1)
    cmd = parts[0].lower()
    args = parts[1] if len(parts) > 1 else ''

    # sudo 前缀: 临时以 root 权限执行后续命令
    if cmd == 'sudo':
        history_cmd.append('sudo')
        return config.root.sudo_run(args.strip())

    if cmd not in AGREE_RUN:
        return ('错误, 未存在命令\n'
                '你可以输入 help 来查询可用命令')

    if cmd in NEED_ARGS and not args.strip():
        return _USAGE[cmd]

    history_cmd.append(cmd)

    if cmd == 'help':
        return config.root.help
    if cmd == 'message':
        return config.root.message
    if cmd == 'turn_off_system':
        return config.root.turn_off_system
    if cmd == 'echo':
        return config.root.echo(args)
    if cmd == 'open':
        return config.root.open_file(args.strip())
    if cmd == 'users':
        return config.root.users(args)
    if cmd == 'auto-output':
        return config.root.auto_output(args.strip())
    if cmd == 'clear':
        return config.root.clear()
    if cmd == 'time':
        return config.root.time()
    if cmd == 'history':
        return config.root.history()
    if cmd == 'ls':
        return config.root.ls()
    if cmd == 'pwd':
        return config.root.pwd()
    if cmd == 'cd':
        return config.root.cd(args)
    if cmd == 'mkdir':
        return config.root.mkdir(args)
    if cmd == 'rm':
        return config.root.rm(args)
    if cmd == 'unzip':
        return config.root.unzip(args)
    if cmd == 'install':
        return config.root.install(args)
    if cmd == 'pkg':
        return config.root.pkg(args)
    if cmd == 'run':
        return config.root.run(args)
    if cmd == 'theme':
        return theme_cmd(args)
    # ---- 系统终端补齐的命令 ----
    if cmd == 'cls':                       # clear 的别名
        return config.root.clear()
    if cmd == 'reboot':
        return config.root.reboot()
    if cmd == 'drivers':
        return config.root.drivers()
    if cmd == 'whoami':
        return config.root.whoami()
    if cmd == 'hostname':
        return config.root.hostname(args)
    if cmd == 'cat':
        return config.root.cat(args.strip())
    return '命令 "' + cmd + '" 暂未实现'


def theme_cmd(args):
    """theme <dark|light> : 切换终端配色 (仅宿主机 PySide6 版有效)"""
    a = (args or '').strip().lower()
    if a not in ('dark', 'light'):
        from host.theme import available_themes
        return '用法: theme <' + '|'.join(available_themes()) + '>'
    # 设定主题 (config.theme 会被 host/theme.get_theme 优先使用)
    try:
        from host.theme import set_theme
        ok = set_theme(a)
    except ImportError:
        # 内核版拼合时不含 host 模块: 退回 config 上记一个标记, 但无 UI 可换肤
        try:
            from core.config import config
            config.theme = a
            ok = True
        except ImportError:
            ok = False
    if not ok:
        return '主题切换失败'
    # 若宿主机 UI 已启动, 立即重新套用样式 (通过 host/ui 暴露的回调)
    try:
        from host.ui_pyside6 import apply_theme_now
        apply_theme_now()
    except (ImportError, Exception):
        pass
    return '终端配色已切换为: ' + a


def cmd_history():
    if not history_cmd:
        return '尚无历史命令'
    return '已运行命令:\n' + '\n'.join(history_cmd)


# 兼容旧调用名
history = cmd_history
