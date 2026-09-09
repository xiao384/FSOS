# ============================================================
# help_ascii.py - 内核版帮助文本 (由 tools/bundle_pt.py 追加到拼合文件末尾)
#
# 内核屏幕是 mode13h 320x200 + 8x8 BIOS 字模, 只能显示 ASCII; 中文会以
# UTF-8 的多字节形式逐字节画出无意义字形。这里用同名常量 HELP_LINES 覆盖
# core/ptos.py 里的中文版, 于是:
#   - 内核终端 (bt 命令)  -> 本 ASCII 版
#   - 宿主机 PySide6 窗口  -> core/ptos.py 的中文版
# 二者共用同一份命令实现, 只是帮助文本不同。
# ============================================================
HELP_LINES = [
    'help                       show this help',
    'message                    system info',
    'turn_off_system            power off (root)',
    'open <path>                open and edit a file',
    'users                      current user info',
    'users list                 list all users',
    'users modify <name>        change password/role (root)',
    'users add                  add a new user (root)',
    'users delete <name>        delete a user (root)',
    'sudo root                  gain root (permanent)',
    'sudo <cmd>                 run one command as root',
    'auto-output <1|2>          auto paste (1=file, 2=text)',
    'reboot bash                restart terminal',
    'clear                      clear screen',
    'time                       current time',
    'echo <text>                print text',
    'history                    command history',
    'ls                         list files',
    'pwd                        current path',
    'cd <path>                  change directory (host)',
    'mkdir <path>               make directory (host)',
    'rm <file>                  delete file (root)',
    '---- packages ----',
    'pkg list                   list embedded/installed packages',
    'pkg remove <pkg>           uninstall a package',
    'unzip <pkg>                extract to filestore',
    'unzip -l <pkg>             list files in a package',
    'install <pkg>              extract and register',
    'run <pkg>                  run entry file of a package',
    '---- system ----',
    'whoami                     current user and role',
    'drivers                    list registered kernel drivers',
    'hostname [name]            show or set hostname',
    'cat <file>                 print a file from filestore',
    'reboot                     reboot the system',
    'cls                        alias of clear',
]
