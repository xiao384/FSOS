# ============================================================
# backend_kernel.py - FSOS 内核文本终端后端
#
# 屏幕为 mode13h 320x200、8x8 字体, 即 40x25 字符, 因此:
#   - 输出直接 print (内核终端把 print 接到屏幕);
#   - 所有"对话框"退化为行输入: confirm 用 y/N, pick 用序号,
#     edit_text 用"独占一行 . 结束"的行输入模式。
#
# 内核兼容: 只依赖内置 input/print。
# ============================================================
from core.backend import Backend


class KernelBackend(Backend):
    name = 'kernel'

    def out(self, text):
        print(text)

    def clear(self):
        # 内核终端的清屏由 terminal.c 处理, 这里只输出分隔
        print('\n' * 12)

    def notify(self, title, msg):
        print('[i] ' + title + ': ' + msg)

    def warn(self, title, msg):
        print('[!] ' + title + ': ' + msg)

    def confirm(self, title, msg, danger=False):
        print(title + ': ' + msg)
        ans = input('确认? (y/N): ')
        return ans.strip().lower() in ('y', 'yes')

    def ask_text(self, title, prompt, secret=False):
        # 内核终端无法隐藏回显, 密码会明文显示
        return input(prompt + ': ')

    def ask_fields(self, title, fields):
        print('== ' + title + ' ==')
        result = {}
        for item in fields:
            result[item[0]] = input(item[1] + ': ')
        return result

    def pick(self, title, options):
        print('== ' + title + ' ==')
        for i in range(len(options)):
            print('  [' + str(i) + '] ' + str(options[i]))
        raw = input('选择序号 (直接回车取消): ').strip()
        if not raw:
            return None
        try:
            idx = int(raw)
        except ValueError:
            return None
        if idx < 0 or idx >= len(options):
            return None
        return idx

    def edit_text(self, title, content):
        print('== ' + title + ' ==')
        print(content)
        print('-- 输入新内容; 独占一行的 "." 结束, 直接 "." 放弃 --')
        lines = []
        while True:
            ln = input('')
            if ln == '.':
                break
            lines.append(ln)
        if not lines:
            return None
        return '\n'.join(lines) + '\n'

    def paste(self, text, count, delay):
        return '自动输出依赖桌面自动化 (pyautogui), 内核中不可用'

    def reboot_app(self):
        # krn.reboot() 不返回; 这里交给调用方决定, 返回提示即可
        return '内核中请用终端的 reboot 命令重启'
