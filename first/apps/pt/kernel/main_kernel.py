# ============================================================
# main_kernel.py - 内核入口
#
# 由 tools/bundle_pt.py 与 core/ pkg/ 各模块拼成单文件 pyroot/bt.py,
# 再由 pyroot/gen_frozen.py 转成 C 字符串编进内核; 系统终端即运行它。
#
# 注意: 本文件本身不含自动调用 run() 的代码 —— 由打包脚本在末尾追加,
#       这样测试脚本可以只导入符号而不启动交互循环。
#
# 自定义终端: 内核屏是 mode13h 320x200 + 8x8 字模 (40x25)。这里不用
#   内置 input()/print() 的简单行输入, 而是经 krn 桥自己绘制返回页与
#   输入行, 从而支持"点击返回页任意一行 -> 在该行就地输入命令 -> 回车执行"。
# ============================================================
import krn
from core.config import config
from core.backend import set_backend
from core.commands import run_cmd_text
from core.ptos import SYSTEM_OS
from kernel.backend_kernel import KernelBackend
from kernel.fs_kernel import KrnFS

# 内核屏是 8x8 字模的 ASCII 终端, 横幅必须是纯 ASCII
# (中文版帮助由 kernel/help_ascii.py 在打包时覆盖, 见 bundle_pt.py)
BANNER = (
    'Better terminal (pt port)\n'
    'Type help for commands, exit to return.\n'
    'Click any line to type there; Enter runs it.'
)


def setup():
    config.krn = krn
    config.fs = KrnFS()
    set_backend(KernelBackend())
    config.users_permission = True
    config.root_permission = True          # 内核终端恒为 root
    config.root = SYSTEM_OS(config.system_name, 'root', 'root', config.fs)
    config.cmd_path = '/'


# ============================================================
# 自定义终端: 返回页 + 可定位输入行
#   默认输入行在底部; 点击返回页某行 -> 该行变为输入行,
#   直接在点击处输入命令, 回车执行 (返回页其余内容保留)。
#   依赖 krn 桥暴露的绘制/键盘/鼠标接口 (见 krn_bridge.c)。
# ============================================================
_BT_COLS = 40
_BT_ROWS = 25
_BT_PROMPT = 'bt> '
_BT_C_INPUT = 10     # COL_LGREEN
_BT_C_TEXT = 15      # COL_WHITE
_BT_C_BLACK = 0

_bt_lines = []          # 返回页历史 (每行一个字符串)
_bt_inrow = _BT_ROWS - 1
_bt_inbuf = ''
_bt_prev_left = False


def _bt_append(text):
    for ln in (text or '').split('\n'):
        if not ln:
            _bt_lines.append('')
            continue
        # 高分辨率终端按当前列宽换行，避免输出被屏幕裁切。
        while len(ln) > _BT_COLS:
            _bt_lines.append(ln[:_BT_COLS])
            ln = ln[_BT_COLS:]
        _bt_lines.append(ln)
    if len(_bt_lines) > 1000:
        del _bt_lines[:-1000]


def _bt_draw():
    _bt_refresh_geometry()
    krn.term_clear()
    start = len(_bt_lines) - (_BT_ROWS - 1)
    if start < 0:
        start = 0
    for i in range(_BT_ROWS - 1):
        li = start + i
        if li < len(_bt_lines):
            krn.term_text(0, i * 8, _bt_lines[li], _BT_C_TEXT)
    # 输入行 (可能位于返回页内任意行)
    y = _bt_inrow * 8
    krn.term_fill(0, y, _BT_COLS * 8 - 1, y + 7, _BT_C_BLACK)
    krn.term_text(0, y, _BT_PROMPT + _bt_inbuf, _BT_C_INPUT)
    # 光标
    cx = (len(_BT_PROMPT) + len(_bt_inbuf)) * 8
    krn.term_fill(cx, y, cx + 5, y + 7, _BT_C_TEXT)
    # 鼠标指针
    try:
        m = krn.mouse()
        krn.term_fill(m[0], m[1], m[0] + 5, m[1] + 7, _BT_C_TEXT)
    except Exception:
        pass


def _bt_input():
    global _bt_inrow, _bt_inbuf, _bt_prev_left
    _bt_refresh_geometry()
    _bt_inbuf = ''
    _bt_inrow = _BT_ROWS - 1
    _bt_draw()
    while True:
        try:
            m = krn.mouse()
            if m[2] and not _bt_prev_left:      # 左键按下边沿 -> 定位输入行
                r = m[1] // 8
                if r < _BT_ROWS - 1:
                    _bt_inrow = r
                    _bt_inbuf = ''
                    _bt_draw()
            _bt_prev_left = m[2]
        except Exception:
            pass
        k = krn.kb_poll()
        if k != 0:
            if k == 13:                        # Enter
                cmd = _bt_inbuf
                _bt_inbuf = ''
                _bt_inrow = _BT_ROWS - 1
                return cmd
            elif k == 8:                       # Backspace
                _bt_inbuf = _bt_inbuf[:-1]
                _bt_draw()
            elif k == 27:                      # ESC: 取消定位, 回到底部输入行
                _bt_inrow = _BT_ROWS - 1
                _bt_inbuf = ''
                _bt_draw()
            elif 32 <= k <= 126:
                _bt_inbuf += chr(k)
                _bt_draw()
        krn.delay(2)                           # 让出时间, 避免空转


def run():
    global _bt_lines, _bt_inrow, _bt_inbuf
    setup()
    _bt_refresh_geometry()
    _bt_lines = []
    _bt_append(BANNER)
    while True:
        cmd = _bt_input()
        cmd = cmd.strip()
        if cmd == '':
            continue
        _bt_append(_BT_PROMPT + cmd)           # 回显命令到返回页
        if cmd == 'exit' or cmd == 'quit':
            break
        # clear: 在自定义终端里必须清空返回页缓冲并真正清屏
        # (走 run_cmd_text 只能打印空行, 下次重绘会把旧内容画回来)
        if cmd == 'clear' or cmd == 'cls':
            _bt_lines = []
            _bt_draw()
            continue
        out = run_cmd_text(cmd)
        if out:
            _bt_append(out)
