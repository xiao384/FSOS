# ============================================================
# test_pt.py - apps/pt 的自检脚本 (宿主机 CPython 下运行)
#
#   python tools/test_pt.py
#
# 覆盖:
#   1. inflate   : 各压缩级别的 raw DEFLATE 往返 (含 store/固定/动态霍夫曼)
#   2. tinyunzip : STORED 与 DEFLATE 两种 zip 的解析与 CRC 校验
#   3. b64       : 编解码往返
#   4. 原项目完好: _upstream 副本与 D:\better terminal_project\pt 逐字节一致
#   5. ptpkg     : 在假 krn 上完成 install / list / run / uninstall
#   6. HostFS    : 宿主机文件区基本操作
#   7. 端到端    : 拼合后的 pyroot/bt.py 在假 krn + 脚本输入下完整跑通
# ============================================================
import io
import os
import random
import sys
import zipfile
import zlib

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
FIRST_DIR = os.path.dirname(TOOLS_DIR)
PT_DIR = os.path.join(FIRST_DIR, 'apps', 'pt')
BUNDLE = os.path.join(FIRST_DIR, 'pyroot', 'bt.py')

if PT_DIR not in sys.path:
    sys.path.insert(0, PT_DIR)

_passed = 0
_failed = 0


def check(name, cond, detail=''):
    global _passed, _failed
    if cond:
        _passed += 1
        print('  [ok]   %s' % name)
    else:
        _failed += 1
        print('  [FAIL] %s %s' % (name, detail))


def section(title):
    print('\n== %s ==' % title)


# ============================================================
section('1. inflate (raw DEFLATE 往返)')
from pkg.inflate import inflate  # noqa: E402

payloads = [
    b'',
    b'a',
    b'hello, world!',
    ('print("hello")\n' * 400).encode('utf-8'),          # 高重复 -> 长距离匹配
    ('中文测试 UTF-8 编解码 ' * 60).encode('utf-8'),
    bytes(random.Random(7).randrange(256) for _ in range(9000)),  # 低压缩率
]
ok = True
for idx, payload in enumerate(payloads):
    for level in (0, 1, 6, 9):
        co = zlib.compressobj(level, zlib.DEFLATED, -15)
        comp = co.compress(payload) + co.flush()
        try:
            got = inflate(comp)
        except Exception as e:
            ok = False
            print('    解压异常 payload=%d level=%d: %s' % (idx, level, e))
            continue
        if got != payload:
            ok = False
            print('    不一致 payload=%d level=%d (%d vs %d 字节)'
                  % (idx, level, len(got), len(payload)))
check('inflate 往返 (6 组数据 x 4 个压缩级别)', ok)

# 损坏数据必须报错而不是死循环
try:
    inflate(b'\xff\xff\xff\xff\xff\xff')
    check('损坏数据抛出异常', False, '未抛异常')
except Exception:
    check('损坏数据抛出异常', True)


# ============================================================
section('2. tinyunzip (zip 解析)')
from pkg.tinyunzip import read_zip, crc32  # noqa: E402

files = {
    'main.py': 'print("hi")\n'.encode('utf-8') * 30,
    'README.TXT': ('中文说明\n' * 50).encode('utf-8'),
    'data.bin': bytes(random.Random(11).randrange(1, 256) for _ in range(3000)),
}
for method, label in ((zipfile.ZIP_STORED, 'STORED'),
                      (zipfile.ZIP_DEFLATED, 'DEFLATE')):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', method) as zf:
        for name, content in sorted(files.items()):
            zf.writestr(name, content)
    got = dict(read_zip(buf.getvalue()))
    same = (set(got.keys()) == set(files.keys()) and
            all(got[k] == files[k] for k in files))
    check('read_zip (%s) 内容与 CRC' % label, same)

check('crc32 与 zlib 一致', crc32(b'123456789') == zlib.crc32(b'123456789'))

bad = io.BytesIO()
with zipfile.ZipFile(bad, 'w', zipfile.ZIP_DEFLATED) as zf:
    zf.writestr('x.txt', b'abcdef')
raw = bytearray(bad.getvalue())
raw[-30] ^= 0xFF          # 破坏中央目录附近的数据
try:
    read_zip(bytes(raw))
    check('损坏 zip 被拒绝', False, '未抛异常')
except Exception:
    check('损坏 zip 被拒绝', True)


# ============================================================
section('3. base64 往返')
from pkg.b64 import b64encode, b64decode  # noqa: E402
from pkg.frozen_pkgs import FROZEN_PKGS  # noqa: E402

_ok = True
for n in (0, 1, 2, 3, 4, 5, 999, 4001):
    data = bytes(random.Random(n).randrange(256) for _ in range(n))
    if b64decode(b64encode(data)) != data:
        _ok = False
check('b64 往返 (含 0/1/2 字节余数)', _ok)
check('内嵌包 hello 已生成', 'hello' in FROZEN_PKGS,
      '实际: %s' % list(FROZEN_PKGS.keys()))


# ============================================================
section('4. 原项目完好性 (只读校验)')
sys.path.insert(0, TOOLS_DIR)
import sync_pt  # noqa: E402

check('_upstream 副本与原项目逐字节一致',
      sync_pt.verify(sync_pt.DEFAULT_SRC, sync_pt.DEFAULT_DEST) == 0)


# ============================================================
section('5. ptpkg 在假 krn 上的安装流程')


class FakeKrn(object):
    """模拟内核 krn 模块的文件区行为: 扁平、单文件 4KB、读缺失时抛异常"""

    def __init__(self):
        self.files = {}

    def message(self):
        return 'FSOS (fake)'

    def users(self):
        return [['root', 'root'], ['guest', 'users']]

    def read_file(self, name):
        if name not in self.files:
            raise OSError(2, 'ENOENT')
        return self.files[name]

    def write_file(self, name, text):
        self.files[name] = str(text)[:4096]
        return 'file written'

    def list_files(self):
        return list(self.files.keys())

    def del_file(self, name):
        if name not in self.files:
            return 'ERROR: no such file'
        del self.files[name]
        return 'file deleted'

    def exec_elf(self, name):
        if name not in self.files:
            return -2
        self.last_elf = name
        return 7

    def run(self, line):
        self.last_run = line
        return None

    # ---- 内核终端绘制/输入桩 (供 section 8 端到端使用) ----
    # 当前 bt.py 内核终端走 krn.kb_poll() 逐键输入 (非 builtins.input), 并把
    # 返回页/输出经 _bt_append 累积到模块级 _bt_lines, 再用 krn.term_text 画屏。
    # 端到端: 用 set_script 把命令脚本转成键码流由 kb_poll 驱动; 断言文本直接从
    # exec 后的 _bt_lines (全量滚动历史) + stdout (run 包体 print) 取, 不依赖屏幕桩。
    def term_clear(self):
        pass

    def term_text(self, x, y, s, c):
        pass

    def term_fill(self, x0, y0, x1, y1, c):
        pass

    def mouse(self):
        return (0, 0, 0)

    def delay(self, ms):
        pass

    def set_script(self, lines):
        self._keys = []
        for ln in lines:
            self._keys += [ord(c) for c in ln] + [13]   # 每行: 字符键码 + 回车
        self._ki = 0

    def kb_poll(self):
        ks = getattr(self, '_keys', None)
        if ks and self._ki < len(ks):
            k = ks[self._ki]
            self._ki += 1
            return k
        return 0


fake = FakeKrn()
sys.modules['krn'] = fake

from kernel.fs_kernel import KrnFS  # noqa: E402
import pkg.ptpkg as ptpkg  # noqa: E402
from core.config import config  # noqa: E402

fs = KrnFS()
check('初始无已安装包', ptpkg.list_packages(fs).startswith('  [内嵌]'))

raw = ptpkg.get_package_bytes(fs, 'hello')
check('取得内嵌包字节', raw is not None and raw[:2] == b'PK')

msg = ptpkg.install_zip(fs, 'hello', raw)
check('install 成功', msg.startswith('已安装 hello'), msg)

names = fs.names()
check('内核 ls 能看到真实文件区',
      'main.py' in names and 'README.TXT' in names, str(names))

# 模拟 C 文件管理器额外创建一个不在 INDEX.TXT 的文件，终端也必须看得到。
fs.write('manual.py', 'print(42)')
check('非包文件也能被终端发现', 'manual.py' in fs.names(), str(fs.names()))

src = ptpkg.app_source(fs, 'hello')
check('入口源码可读', src is not None and 'hello from an installed' in src)

listed = ptpkg.list_packages(fs)
check('pkg list 同时显示内嵌与已装',
      '[内嵌] hello' in listed and '[已装] hello' in listed)

msg = ptpkg.uninstall(fs, 'hello')
check('uninstall 成功', msg.startswith('已卸载'), msg)
check('卸载后包文件被真正删除', 'main.py' not in fs.names() and 'README.TXT' not in fs.names())
check('非包文件不受卸载影响', 'manual.py' in fs.names())

# 超过 4KB 的文件必须被拒绝, 而不是被静默截断
big = io.BytesIO()
with zipfile.ZipFile(big, 'w', zipfile.ZIP_DEFLATED) as zf:
    zf.writestr('big.txt', 'x' * 9000)
msg = ptpkg.install_zip(fs, 'big', big.getvalue())
check('超 4KB 的文件被拒绝', msg.startswith('安装失败'), msg)

# package 分块覆盖回归：新包更小时必须清掉旧高位分块。
ptpkg.store_package(fs, 'chunkcase', b'A' * 7000)
old_tail = fs.read('chunkcase.P2') if fs.exists('chunkcase.P2') else ''
check('分块测试创建旧尾块', old_tail != '')
ptpkg.store_package(fs, 'chunkcase', b'B')
check('小包覆盖会删除旧尾块', not fs.exists('chunkcase.P2'))
check('小包覆盖后可正确读取', ptpkg.get_package_bytes(fs, 'chunkcase') == b'B')

# run 扩展：直接运行脚本/ELF，而不仅是已安装包。
fs.write('hello.py', 'print("direct python")\n')
fs.write('build.sh', '#!/bin/sh\necho step1\necho step2\n')
fs.write('HELLO.ELF', 'not-text-placeholder')
from core.ptos import SYSTEM_OS  # noqa: E402
config.krn = fake
config.root = SYSTEM_OS('pxs', 'root', 'root', fs)
out_py = config.root.run('hello.py')
check('run 可直接执行 .py', out_py == 'Python 脚本执行完成: hello.py', repr(out_py))
out_sh = config.root.run('build.sh')
check('run 可逐行执行 .sh', '[build.sh:2] step1' in out_sh and '[build.sh:3] step2' in out_sh, repr(out_sh))
out_elf = config.root.run('HELLO.ELF')
check('run 可启动 .elf', out_elf == 'ELF 启动请求已提交: HELLO.ELF' and getattr(fake, 'last_run', '') == 'run HELLO.ELF', repr(out_elf))


# ============================================================
section('6. HostFS (宿主机文件区)')
import tempfile  # noqa: E402
from core.fs_host import HostFS  # noqa: E402

tmp = tempfile.mkdtemp(prefix='ptfs_')
hfs = HostFS(tmp)
hfs.write('a.txt', 'hello host')
check('HostFS 写入/读取', hfs.read('a.txt') == 'hello host')
check('HostFS 列出', 'a.txt' in hfs.names())
check('HostFS mkdir', hfs.mkdir('sub').startswith('目录已创建'))
check('HostFS cd', hfs.chdir('sub').startswith('工作路径已切换'))
# 注意: 上面已切进 sub, 删除必须回到根目录再按相对路径删除
check('HostFS cd 回上级', hfs.chdir('..').startswith('工作路径已切换'))
check('HostFS 删除进回收站', hfs.delete('a.txt').startswith('已删除'))


# ============================================================
section('7. 命令分发 (theme 等)')

from core.commands import run_cmd_text, AGREE_RUN  # noqa: E402
from core.config import config  # noqa: E402
config.system_name = 'nt'
config.root = None

check('theme 已注册为命令', 'theme' in AGREE_RUN)

# 内核/CLI 环境没有 host 包: theme 应降级到 config.theme 并记录偏好
out_dark = run_cmd_text('theme dark')
check('theme dark 降级成功', out_dark == '终端配色已切换为: dark', repr(out_dark))
check('config.theme 被记录', getattr(config, 'theme', None) == 'dark')
out_bad = run_cmd_text('theme rainbow')
check('theme 非法参数给用法', out_bad.startswith('用法: theme'), repr(out_bad))

# 宿主机有 host 包: 走 host.theme.set_theme
try:
    from host.theme import get_theme, available_themes  # noqa: E402
    out_light = run_cmd_text('theme light')
    check('theme light (host) 成功',
          out_light == '终端配色已切换为: light', repr(out_light))
    check('host 提供两色主题', set(available_themes()) >= {'dark', 'light'})
    check('get_theme 反映手动选择', get_theme().get('name') == 'light')
except ImportError:
    check('theme (host 版) 跳过', True, 'host 包不可用 (非必需)')


# ============================================================
section('8. MicroPython FSOS 端口静态检查')
MPCONFIG = os.path.join(FIRST_DIR, 'micropython', 'ports', 'fsos', 'mpconfigport.h')
MPENTRY = os.path.join(FIRST_DIR, 'micropython', 'ports', 'fsos', 'mp_entry.c')
try:
    cfg = open(MPCONFIG, 'r', encoding='utf-8').read()
    ent = open(MPENTRY, 'r', encoding='utf-8').read()
    check('FSOS 开启外部 Python import', '#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)' in cfg)
    check('端口实现文件存在性探测', 'mp_import_stat_t mp_import_stat' in ent and 'fs_size(path)' in ent)
    check('端口实现 Python 文件读取', 'mp_lexer_new_from_file' in ent and 'fs_read(path, buf' in ent)
    check('Better Terminal 编译对象注册为 GC root',
          'static mp_obj_t g_bt_fun' in ent and 'gc_collect_root(&g_bt_fun, 1)' in ent)
    check('MicroPython 初始化有异常保护',
          'mp_fsos_run(int mode)' in ent and 'nlr_push(&nlr)' in ent)
except Exception as e:
    check('MicroPython 端口静态检查', False, str(e))


# ============================================================
section('9. 端到端: 运行拼合后的 pyroot/bt.py')
import builtins  # noqa: E402

if not os.path.isfile(BUNDLE):
    check('存在 pyroot/bt.py', False, BUNDLE)
else:
    with open(BUNDLE, 'r', encoding='utf-8') as f:
        bundle_src = f.read()

    fake2 = FakeKrn()
    fake2.set_script([
        'help',
        'message',
        'users list',
        'pkg list',
        'install hello',
        'ls',
        'run hello',
        'unzip -l hello',
        'pkg remove hello',
        'ls',
        'exit',
    ])
    sys.modules['krn'] = fake2

    # 内核终端输入走 krn.kb_poll() (见 bt.py), 由 set_script 驱动;
    # 保留 builtins.input 安全桩, 防止万一触发的确认对话框卡在真实 stdin。
    out = io.StringIO()
    old_stdout = sys.stdout
    old_input = getattr(builtins, 'input', None)

    def _safe_input(prompt=''):
        return 'y'

    builtins.input = _safe_input
    sys.stdout = out
    ns = {'__name__': '__main__'}
    try:
        exec(compile(bundle_src, BUNDLE, 'exec'), ns)
    except Exception as e:
        import traceback
        sys.stdout = old_stdout
        traceback.print_exc()
        check('端到端运行', False, str(e))
    else:
        check('端到端运行', True)
    finally:
        sys.stdout = old_stdout
        if old_input is not None:
            builtins.input = old_input
    # 全量滚动历史 (_bt_lines) + run 包体 print (stdout) 合并为可断言文本
    bt_lines = ns.get('_bt_lines', [])
    text = '\n'.join(bt_lines) + '\n' + out.getvalue()
    # 内核版 help 被 kernel/help_ascii.py 覆盖为 ASCII
    check('help 输出包管理命令 (ASCII 内核版)', 'install <pkg>' in text)
    check('横幅与帮助为纯 ASCII (内核屏可显示)',
          not any(ord(c) > 127 for c in text.split('message')[0]))
    check('message 走 krn', 'FSOS (fake)' in text)
    check('users list 走 krn', 'root' in text and 'guest' in text)
    check('pkg list 显示内嵌包', '[内嵌] hello' in text)
    check('install 成功', '已安装 hello' in text)
    check('run 执行了包入口', 'hello from an installed package!' in text)
    check('run 执行了中文输出', '你好, FSOS' in text)
    check('unzip -l 列出包内文件', 'README.TXT' in text and '字节' in text)
    check('pkg remove 成功', '已卸载: hello' in text)


# ============================================================
print('\n' + '=' * 46)
print('通过 %d 项, 失败 %d 项' % (_passed, _failed))
print('=' * 46)
sys.exit(1 if _failed else 0)
