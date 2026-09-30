# 由 tools/bundle_pt.py 自动生成, 请勿手工编辑。
# 源文件: apps/pt 下的 core/ pkg/ kernel/ (见 BUNDLE_ORDER)

# ====================================================================
# from core/backend.py
# ====================================================================
# ============================================================
# backend.py - 界面后端接口 (核心逻辑与具体 GUI 之间的隔离层)
#
# 由 _upstream/system_os.py + _upstream/ui.py 改造而来:
#   原实现把 tk 对话框直接写在业务逻辑里 (tk.Toplevel / wait_window /
#   messagebox), 导致同一份代码无法脱离 tkinter 运行。这里把所有"显示与
#   交互"收敛成一组后端方法, 于是同一份核心既能:
#     - 跑在 PySide6 桌面窗口 (host/backend_pyside6.py)
#     - 跑在 FSOS 内核的 320x200 文本终端 (kernel/backend_kernel.py)
#     - 跑在纯命令行 (CliBackend, 供无 GUI 环境与自动化测试使用)
#
# 约定: 交互类方法返回 None 一律表示"用户取消"。
#
# 内核兼容: 不使用 f-string / str.format / % 格式化 / 任何第三方或
#           非内置模块 (MicroPython 处于 ROM_LEVEL_MINIMUM)。
# ============================================================


class Backend(object):
    name = 'null'

    # ---- 输出 ----
    def out(self, text):
        print(text)

    def clear(self):
        pass

    def set_title(self, title):
        pass

    # ---- 提示 ----
    def notify(self, title, msg):
        self.out('[i] ' + title + ': ' + msg)

    def warn(self, title, msg):
        self.out('[!] ' + title + ': ' + msg)

    # ---- 交互 ----
    def confirm(self, title, msg, danger=False):
        return False

    def ask_text(self, title, prompt, secret=False):
        return None

    def ask_fields(self, title, fields):
        """fields: [(key, label, secret), ...] 返回 dict 或 None"""
        return None

    def pick(self, title, options):
        """返回选中项的下标 (int) 或 None"""
        return None

    def edit_text(self, title, content):
        """编辑一段文本, 返回新内容或 None (放弃修改)"""
        return None

    # ---- 桌面自动化 (仅宿主机有意义) ----
    def paste(self, text, count, delay):
        return '自动输出在当前后端不可用'

    # ---- 重启终端 ----
    def reboot_app(self):
        return '重启终端在当前后端不可用'


class CliBackend(Backend):
    """纯命令行后端: 无 GUI 环境 / 自动化测试用"""

    name = 'cli'

    def confirm(self, title, msg, danger=False):
        self.out(title + ': ' + msg)
        ans = input('确认? (y/N): ')
        return ans.strip().lower() in ('y', 'yes')

    def ask_text(self, title, prompt, secret=False):
        return input(title + ' - ' + prompt + ': ')

    def ask_fields(self, title, fields):
        self.out('== ' + title + ' ==')
        result = {}
        for item in fields:
            key = item[0]
            label = item[1]
            result[key] = input(label + ': ')
        return result

    def pick(self, title, options):
        self.out('== ' + title + ' ==')
        for i in range(len(options)):
            self.out('  [' + str(i) + '] ' + str(options[i]))
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
        self.out('== ' + title + ' ==')
        self.out(content)
        self.out('-- 输入新内容; 独占一行的 "." 结束, 直接 "." 表示放弃 --')
        lines = []
        while True:
            ln = input('')
            if ln == '.':
                break
            lines.append(ln)
        if not lines:
            return None
        return '\n'.join(lines) + '\n'


_backend = None


def set_backend(backend):
    global _backend
    _backend = backend
    return backend


def get_backend():
    global _backend
    if _backend is None:
        _backend = Backend()
    return _backend

# ====================================================================
# from core/config.py
# ====================================================================
# ============================================================
# config.py - 全局共享运行时状态
# 由 _upstream/config.py 改造:
#   - 去掉 tk 窗口引用 (main_tk/sign_tk/tk_input_* 等), 界面状态改由各后端
#     自行持有;
#   - 增加 krn / fs 两个运行时插槽: 内核里指向 MicroPython 的 krn 模块与
#     KrnFS, 宿主机里为 None 并走 os / HostFS。
#
# 关键约束 (为内核打包服务):
#   状态集中在一个 config 对象上, 而不是散落为模块级变量。这样
#   tools/bundle_pt.py 把多个模块拼成单文件时, 只需删掉
#   "from core.config import config" 这一行, 后面的代码无需改动 ——
#   config 这个名字在拼合后的文件里依然指向同一个对象。
# ============================================================
try:
    import os as _os

    _system_name = _os.name
    # __file__ 在内核里不存在: 内核是用 mp_exec_str 执行一整段源码字符串,
    # 没有"文件"的概念。此处必须防御, 否则内核启动即在 config 处 NameError。
    try:
        _run_path = _os.path.dirname(_os.path.abspath(__file__))
    except NameError:
        _run_path = '/'
except ImportError:
    _system_name = 'pxs'
    _run_path = '/'

# 内核里存在 krn 模块 (C 侧桥接), 宿主机里没有
try:
    import krn as _krn
except ImportError:
    _krn = None


class _Config(object):
    pass


config = _Config()
config.system_name = _system_name
config.run_path = _run_path
config.cmd_path = _run_path     # 当前工作路径 (仅宿主机有意义)
config.krn = _krn               # 内核桥接模块, 宿主机为 None
config.root_permission = False  # 是否拥有 root 权限
config.users_permission = False  # 是否登录成功
config.root = None              # 当前登录的 SYSTEM_OS 实例
config.fs = None                # 文件区抽象 (KrnFS / HostFS)

# ====================================================================
# from pkg/b64.py
# ====================================================================
# ============================================================
# b64.py - 纯 Python base64 编解码
#
# 为什么需要它:
#   内核的 krn.read_file() 以首个 NUL 作为字符串结束, 无法存放二进制。
#   因此内嵌到内核里的 .zip 包统一以 base64 文本形式携带。
#   MicroPython 在 ROM_LEVEL_MINIMUM 下没有 binascii / base64 模块。
# ============================================================

_B64_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
_B64_PAD = '='

_B64_REVERSE = {}
for _i in range(len(_B64_ALPHABET)):
    _B64_REVERSE[_B64_ALPHABET[_i]] = _i


def b64encode(data):
    data = bytes(data)
    out = []
    i = 0
    n = len(data)
    while i < n:
        rest = n - i
        if rest >= 3:
            chunk = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2]
            out.append(_B64_ALPHABET[(chunk >> 18) & 0x3F])
            out.append(_B64_ALPHABET[(chunk >> 12) & 0x3F])
            out.append(_B64_ALPHABET[(chunk >> 6) & 0x3F])
            out.append(_B64_ALPHABET[chunk & 0x3F])
            i += 3
        elif rest == 2:
            chunk = (data[i] << 16) | (data[i + 1] << 8)
            out.append(_B64_ALPHABET[(chunk >> 18) & 0x3F])
            out.append(_B64_ALPHABET[(chunk >> 12) & 0x3F])
            out.append(_B64_ALPHABET[(chunk >> 6) & 0x3F])
            out.append(_B64_PAD)
            i += 2
        else:
            chunk = data[i] << 16
            out.append(_B64_ALPHABET[(chunk >> 18) & 0x3F])
            out.append(_B64_ALPHABET[(chunk >> 12) & 0x3F])
            out.append(_B64_PAD)
            out.append(_B64_PAD)
            i += 1
    return ''.join(out)


def b64decode(text):
    out = bytearray()
    acc = 0
    nbits = 0
    for ch in text:
        if ch == _B64_PAD:
            break
        if ch == '\n' or ch == '\r' or ch == ' ' or ch == '\t':
            continue
        if ch not in _B64_REVERSE:
            raise ValueError('非 base64 字符: ' + ch)
        acc = (acc << 6) | _B64_REVERSE[ch]
        nbits += 6
        if nbits >= 8:
            nbits -= 8
            out.append((acc >> nbits) & 0xFF)
    return bytes(out)

# ====================================================================
# from pkg/inflate.py
# ====================================================================
# ============================================================
# inflate.py - 纯 Python 的 DEFLATE (RFC 1951) 解压
#
# 为什么需要它:
#   内核里的 MicroPython 处于 ROM_LEVEL_MINIMUM, 没有 uzlib / zlib 模块,
#   二进制包 (.zip) 必须能自己解压。这段实现不依赖任何非内置模块,
#   因此同一份代码在宿主机 CPython 与内核 MicroPython 下都能运行。
#
# 内核兼容: 无 f-string / % / str.format / 非内置模块;
#           位运算基于 Python 的任意精度整数, 与 CPython 行为一致。
# ============================================================

LEN_BASE = (3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35,
            43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258)
LEN_EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
             4, 4, 4, 4, 5, 5, 5, 5, 0)
DIST_BASE = (1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
             257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
             8193, 12289, 16385, 24577)
DIST_EXTRA = (0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
              9, 9, 10, 10, 11, 11, 12, 12, 13, 13)
CL_ORDER = (16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15)

MAX_BITS = 15


class _BitReader(object):
    """LSB-first 位读取器, 与 DEFLATE 的位序一致"""

    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos
        self.buf = 0
        self.cnt = 0

    def bits(self, n):
        while self.cnt < n:
            self.buf |= self.data[self.pos] << self.cnt
            self.pos += 1
            self.cnt += 8
        val = self.buf & ((1 << n) - 1)
        self.buf >>= n
        self.cnt -= n
        return val

    def align(self):
        """回到字节边界: 丢弃不足一字节的部分, 并把整字节退回输入流"""
        drop = self.cnt & 7
        self.buf >>= drop
        self.cnt -= drop
        while self.cnt >= 8:
            self.cnt -= 8
            self.pos -= 1
        self.buf = 0
        self.cnt = 0


def _huffman(lengths):
    """由码长表构造规范霍夫曼表: {(码长<<16)|码值: 符号}"""
    table = {}
    count = [0] * (MAX_BITS + 1)
    for ln in lengths:
        if ln:
            count[ln] += 1
    code = 0
    next_code = [0] * (MAX_BITS + 2)
    for nbits in range(1, MAX_BITS + 1):
        code = (code + count[nbits - 1]) << 1
        next_code[nbits] = code
    for sym in range(len(lengths)):
        ln = lengths[sym]
        if ln:
            table[(ln << 16) | next_code[ln]] = sym
            next_code[ln] += 1
    return table


def _decode(reader, table):
    """逐位下降解码 (简单可靠, 不要求速度)"""
    code = 0
    length = 0
    while length < MAX_BITS:
        code = (code << 1) | reader.bits(1)
        length += 1
        key = (length << 16) | code
        if key in table:
            return table[key]
    raise ValueError('无效的霍夫曼编码')


_fixed_cache = None


def _fixed_tables():
    global _fixed_cache
    if _fixed_cache is None:
        lit_lengths = [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8
        dist_lengths = [5] * 30
        _fixed_cache = (_huffman(lit_lengths), _huffman(dist_lengths))
    return _fixed_cache


def _stored(reader, out):
    data = reader.data
    reader.align()
    if reader.pos + 4 > len(data):
        raise ValueError('存储块长度不足')
    ln = data[reader.pos] | (data[reader.pos + 1] << 8)
    nln = data[reader.pos + 2] | (data[reader.pos + 3] << 8)
    reader.pos += 4
    if ln != (~nln & 0xFFFF):
        raise ValueError('存储块长度校验失败')
    if reader.pos + ln > len(data):
        raise ValueError('存储块数据不足')
    out.extend(data[reader.pos:reader.pos + ln])
    reader.pos += ln


def _block(reader, out, lit_table, dist_table):
    while True:
        sym = _decode(reader, lit_table)
        if sym < 256:
            out.append(sym)
        elif sym == 256:
            return
        else:
            sym -= 257
            if sym >= 29:
                raise ValueError('无效的长度码')
            length = LEN_BASE[sym] + reader.bits(LEN_EXTRA[sym])
            dsym = _decode(reader, dist_table)
            if dsym >= 30:
                raise ValueError('无效的距离码')
            dist = DIST_BASE[dsym] + reader.bits(DIST_EXTRA[dsym])
            if dist > len(out):
                raise ValueError('距离超出已输出数据')
            start = len(out) - dist
            for i in range(length):
                out.append(out[start + i])


def _dynamic(reader, out):
    hlit = reader.bits(5) + 257
    hdist = reader.bits(5) + 1
    hclen = reader.bits(4) + 4

    cl_lengths = [0] * 19
    for i in range(hclen):
        cl_lengths[CL_ORDER[i]] = reader.bits(3)
    cl_table = _huffman(cl_lengths)

    lengths = []
    total = hlit + hdist
    while len(lengths) < total:
        sym = _decode(reader, cl_table)
        if sym < 16:
            lengths.append(sym)
        elif sym == 16:
            if not lengths:
                raise ValueError('无效的重复码 16')
            prev = lengths[-1]
            rep = 3 + reader.bits(2)
            for _ in range(rep):
                lengths.append(prev)
        elif sym == 17:
            rep = 3 + reader.bits(3)
            for _ in range(rep):
                lengths.append(0)
        else:
            rep = 11 + reader.bits(7)
            for _ in range(rep):
                lengths.append(0)

    lit_table = _huffman(lengths[:hlit])
    dist_table = _huffman(lengths[hlit:total])
    _block(reader, out, lit_table, dist_table)


def inflate(data):
    """解压一段 raw DEFLATE 数据, 返回 bytes"""
    reader = _BitReader(bytes(data), 0)
    out = bytearray()
    while True:
        last = reader.bits(1)
        btype = reader.bits(2)
        if btype == 0:
            _stored(reader, out)
        elif btype == 1:
            lit_table, dist_table = _fixed_tables()
            _block(reader, out, lit_table, dist_table)
        elif btype == 2:
            _dynamic(reader, out)
        else:
            raise ValueError('无效的块类型: ' + str(btype))
        if last:
            break
    return bytes(out)

# ====================================================================
# from pkg/tinyunzip.py
# ====================================================================
# ============================================================
# tinyunzip.py - 极简 ZIP 读取器 (宿主机 / 内核通用)
#
# 设计取舍:
#   - 只解析"中央目录" (End Of Central Directory -> Central Directory
#     Header -> Local File Header)。相比扫描 local header, 中央目录里的
#     压缩后长度/CRC 才是权威值, 能正确处理带 data descriptor 的条目。
#   - 支持 method 0 (store) 与 method 8 (deflate); 其余方法报错。
#   - 不做 zip64 / 加密 / 分卷支持。
#
# 内核兼容: 不使用 struct / int.from_bytes / zlib, 全部手工按小端解析。
# ============================================================

_EOCD_SIG = 0x06054B50
_CDH_SIG = 0x02014B50
_LFH_SIG = 0x04034B50

_NAME_MAX = 24   # 内核 fs_entry_t 的 name 字段宽度, 文件名超长需拒绝


def _u16(b, off):
    return b[off] | (b[off + 1] << 8)


def _u32(b, off):
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)


def _to_text(raw):
    """把路径字节转成文本: 只保证 ASCII, 其余替换为 '?'"""
    out = []
    for i in range(len(raw)):
        c = raw[i]
        if c < 0x20 or c > 0x7E:
            out.append('?')
        else:
            out.append(chr(c))
    return ''.join(out)


_CRC_TABLE = None


def _crc_table():
    global _CRC_TABLE
    if _CRC_TABLE is None:
        table = []
        for i in range(256):
            c = i
            for _ in range(8):
                if c & 1:
                    c = (c >> 1) ^ 0xEDB88320
                else:
                    c >>= 1
            table.append(c)
        _CRC_TABLE = table
    return _CRC_TABLE


def crc32(data):
    table = _crc_table()
    crc = 0xFFFFFFFF
    for byte in bytes(data):
        crc = ((crc >> 8) ^ table[(crc ^ byte) & 0xFF]) & 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


def read_zip(data, verify=True):
    """解析 zip, 返回 [(name, content_bytes), ...]"""
    data = bytes(data)
    n = len(data)

    # ---- 定位 EOCD (从尾部向前找, 覆盖末尾注释等情况) ----
    eocd = -1
    i = n - 22
    limit = n - 22 - 65536
    if limit < 0:
        limit = 0
    while i >= limit:
        if _u32(data, i) == _EOCD_SIG:
            eocd = i
            break
        i -= 1
    if eocd < 0:
        raise ValueError('不是有效的 zip 文件 (未找到 EOCD)')

    count = _u16(data, eocd + 10)
    cd_off = _u32(data, eocd + 16)

    entries = []
    p = cd_off
    for _ in range(count):
        if p + 46 > n or _u32(data, p) != _CDH_SIG:
            raise ValueError('中央目录损坏')
        method = _u16(data, p + 10)
        crc_want = _u32(data, p + 16)
        csize = _u32(data, p + 20)
        usize = _u32(data, p + 24)
        nlen = _u16(data, p + 28)
        elen = _u16(data, p + 30)
        clen = _u16(data, p + 32)
        lho = _u32(data, p + 42)
        name = _to_text(data[p + 46:p + 46 + nlen])

        if _u32(data, lho) != _LFH_SIG:
            raise ValueError('本地文件头损坏: ' + name)
        lnlen = _u16(data, lho + 26)
        lelen = _u16(data, lho + 28)
        body = lho + 30 + lnlen + lelen
        raw = data[body:body + csize]

        if method == 0:
            content = raw
        elif method == 8:
            content = inflate(raw)
        else:
            raise ValueError('不支持的压缩方式 ' + str(method) + ': ' + name)

        if len(content) != usize:
            raise ValueError('解压后长度不符: ' + name)
        if verify and crc32(content) != crc_want:
            raise ValueError('CRC 校验失败: ' + name)

        entries.append((name, content))
        p += 46 + nlen + elen + clen

    return entries


def entry_names(data):
    return [name for name, _ in read_zip(data, verify=False)]


def valid_file_name(name):
    """内核文件名长度上限检查 (宿主机不受此限制)"""
    return len(name) <= _NAME_MAX

# ====================================================================
# from pkg/frozen_pkgs.py
# ====================================================================
# 由 tools/bundle_pt.py 自动生成, 请勿手工编辑
# 内嵌包: 打包进内核的 .zip (base64), 供内核终端 install <包名> 使用
FROZEN_PKGS = {
    'hello': ('示例包: 演示 install / run 的安装与运行流程',
            'UEsDBBQAAAAIACB3L10jMmONEQIAAPgCAAAKAAAAUkVBRE1FLlRYVGVST08aQRS/k/AdXjxBInI3rYceeu3BY9M0a7sV4rK7YdcmesJGuosshUS0BTFgkGKwAYxRtrsCH6Y7M7vfom+Y1hI7ySSTl/m99/vzMrKiaMAuvWBWJk4xHnu+dOKxeCycN+i3UdSswgvZNOU84M1lVUmBhG4C6/v0ssJKdhICtxDeTcjnIrlzwvkZPmjHZc1D+lDH9th7FVj9ip73ooETjg7isRVaOsYypDYg7HfJlzJ/kWEp7IravBZeOCt8/vlFdDwLJ6NfhU+cEm9zewIJ0rqCl5uvNuHPLN9m16PIcpLr/Bfg2TLh6eHUir2nYsR3fWcblKyxDCL2V2J5pDrCoZxsQgjEJ+1Y4XAcuB68FrU3SdElqxqmpCiQWVgrhqI+p0jsMT21Av+eOB75ec8aPjYQGMX4jyht91irzJGWh65xJxdgAcjvqssDFoBSHw1DcaTahZyUVdf0vX+68nJO+yg/YkjFDadT7hOX0voRetdkfINJY4yBO6CNYVRowtp+Vl/nsbFbn/nt0BoQ9zsyAawDmfRIcUKPjgLfp4dVdjYk0xOR6Wo8Rusz1AuSrhtp3Uzr0rsdaVs20s+4EbXKRhoXpryK0VVE0H8j0/fMjKaCqWmKkd7aVd8r8lvdRCmQSn3Ia/uyuiA9vgkempFVoadjVm9Tu/aYCzpL7AnWxVbgPiBhsRvUq7HOwWKLfgNQSwMEFAAAAAgAIHcvXXkKEofRAAAABAEAAAcAAABtYWluLnB5U1bISM3JyVfQVXg5q1+hoETh+e6O56vXP21rfbq15/msludLdj3Z1/20p5WXS1nh+ZSNCpl5xSWJOTlQbS+WLwbKPW2b+bR16bNp7U92b3vas+vphD4doNoVCkWleVB1zzqXv1jY82zOGoiixw1NvFwFRZl5JRrqEAVpRfm5Col5MONTUxQKEpOzE9NTFdU14UoNFbQVDBVs1XUUwCygBAimpKYppBelppZolGfka1rxcikAAVTHk70Lni7dq6OgDlQPkoXogKhWdwv2DwaZDgBQSwECFAAUAAAACAAgdy9dIzJjjRECAAD4AgAACgAAAAAAAAAAAAAAtoEAAAAAUkVBRE1FLlRYVFBLAQIUABQAAAAIACB3L115ChKH0QAAAAQBAAAHAAAAAAAAAAAAAAC2gTkCAABtYWluLnB5UEsFBgAAAAACAAIAbQAAAC8DAAAAAA=='),
}

# ====================================================================
# from pkg/ptpkg.py
# ====================================================================
# ============================================================
# ptpkg.py - 包管理: 解压 / 安装 / 卸载 / 运行
#
# 包的形态就是一个普通 .zip (用系统 zipfile、7-Zip 等任意工具生成均可),
# 这样"打包"不需要任何自定义工具; 本模块只负责在读侧解析它。
#
# 两种包来源:
#   1) 内嵌包 FROZEN_PKGS: 打包进内核镜像的 base64 常量 (pkg/frozen_pkgs.py),
#      由 tools/bundle_pt.py 从 packages/<name>/ 目录自动生成。
#   2) 文件区包: 已放在文件区里的 base64 分块 (<name>.P0 .P1 ...),
#      便于宿主机侧或后续从外部导入的包。
#
# 索引 INDEX.TXT: 每行 "包名|入口文件|文件1,文件2,...", 记录"已安装"。
#   Better Terminal 通过 krn.list_files() 获取真实文件区清单；INDEX.TXT 只负责记录包元数据。
#
# 内核限制 (来自 krn_bridge.c 的文件区): 单文件 4KB、最多 16 个文件、
#   文件名 24 字符、内容以首个 NUL 结尾 (因此只能存文本)。
# ============================================================

INDEX_NAME = 'INDEX.TXT'
MAX_FILE_BYTES = 4096       # krn 单文件上限 (8 扇区 x 512)
MAX_NAME_LEN = 24            # krn fs_entry_t.name 宽度
CHUNK_BYTES = 3000           # base64 分块前的原始字节数 (编码后 4000 < 4096)


# ============================================================
# 编码
# ============================================================
def utf8_decode(data):
    """最小 UTF-8 解码 (MicroPython 无 bytes.decode)"""
    out = []
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        if b < 0x80:
            out.append(chr(b))
            i += 1
        elif b >= 0xF0:
            cp = (((b & 0x07) << 18) | ((data[i + 1] & 0x3F) << 12) |
                  ((data[i + 2] & 0x3F) << 6) | (data[i + 3] & 0x3F))
            out.append(chr(cp))
            i += 4
        elif b >= 0xE0:
            cp = (((b & 0x0F) << 12) | ((data[i + 1] & 0x3F) << 6) |
                  (data[i + 2] & 0x3F))
            out.append(chr(cp))
            i += 3
        elif b >= 0xC0:
            cp = ((b & 0x1F) << 6) | (data[i + 1] & 0x3F)
            out.append(chr(cp))
            i += 2
        else:
            raise ValueError('非 UTF-8 字节: 0x' + _hex2(b))
    return ''.join(out)


def _hex2(v):
    d = '0123456789ABCDEF'
    return d[(v >> 4) & 0xF] + d[v & 0xF]


def as_text(content):
    """zip 内的文件转为可写入文件区的文本; 二进制内容明确拒绝"""
    for b in content:
        if b == 0:
            raise ValueError('文件含二进制内容 (NUL), 文件区只能存放文本')
    return utf8_decode(content)


# ============================================================
# 索引
# ============================================================
def parse_index(text):
    pkgs = []
    for line in text.split('\n'):
        line = line.strip()
        if not line:
            continue
        parts = line.split('|')
        if len(parts) < 3:
            continue
        files = [f for f in parts[2].split(',') if f]
        pkgs.append({'name': parts[0], 'entry': parts[1], 'files': files})
    return pkgs


def render_index(pkgs):
    lines = []
    for p in pkgs:
        lines.append(p['name'] + '|' + p['entry'] + '|' + ','.join(p['files']))
    return '\n'.join(lines)


def read_index(fs):
    try:
        return parse_index(fs.read(INDEX_NAME))
    except Exception:
        return []


def write_index(fs, pkgs):
    fs.write(INDEX_NAME, render_index(pkgs))


def _find(pkgs, name):
    for p in pkgs:
        if p['name'] == name:
            return p
    return None


# ============================================================
# 文件区分块存取 (base64)
# ============================================================
def chunk_name(base, idx):
    return base + '.P' + str(idx)


def store_package(fs, base, raw):
    """把一个 zip 以 base64 分块写入文件区, 返回分块数"""
    text = b64encode(raw)
    chunks = []
    i = 0
    while i < len(text):
        chunks.append(text[i:i + CHUNK_BYTES])
        i += CHUNK_BYTES
    if len(chunks) > 99:
        raise ValueError('包过大, 分块数超过 99')
    for idx in range(len(chunks)):
        fs.write(chunk_name(base, idx), chunks[idx])
    # 覆盖较小的新包时，删除旧包遗留的高编号分块；否则 load_chunks()
    # 会把旧尾巴也拼进去，导致解码失败或加载到错误内容。
    idx = len(chunks)
    while idx < 100:
        old_name = chunk_name(base, idx)
        try:
            if not fs.exists(old_name):
                break
            fs.delete(old_name)
        except Exception:
            break
        idx += 1
    return len(chunks)


def load_chunks(fs, base):
    parts = []
    idx = 0
    while idx < 100:
        try:
            parts.append(fs.read(chunk_name(base, idx)))
        except Exception:
            break
        idx += 1
    if not parts:
        return None
    return ''.join(parts)


def get_package_bytes(fs, name):
    """按 内嵌包 -> 文件区分块 的顺序取得包的原始 zip 字节"""
    if name in FROZEN_PKGS:
        return b64decode(FROZEN_PKGS[name][1])
    text = load_chunks(fs, name)
    if text is None:
        return None
    return b64decode(text)


# ============================================================
# 安装 / 卸载 / 运行
# ============================================================
def pick_entry(files):
    for f in files:
        if f == 'main.py':
            return f
    for f in files:
        if f.endswith('.py'):
            return f
    return ''


def install_zip(fs, name, zipbytes, entry=None):
    """把 zip 解包写入文件区并登记索引"""
    entries = read_zip(zipbytes)
    entries = [(n, c) for (n, c) in entries if not n.endswith('/')]
    if not entries:
        return '包内没有文件: ' + name
    if len(entries) > 16:
        return '文件数超过内核上限 16: ' + str(len(entries))

    written = []
    try:
        for fname, content in entries:
            if len(fname) > MAX_NAME_LEN:
                raise ValueError('文件名过长 (上限 ' + str(MAX_NAME_LEN) + '): ' + fname)
            if len(content) > MAX_FILE_BYTES:
                raise ValueError('文件过大 (上限 ' + str(MAX_FILE_BYTES) +
                                 ' 字节): ' + fname)
            fs.write(fname, as_text(content))
            written.append(fname)
    except Exception as e:
        return '安装失败: ' + str(e)

    if entry is None:
        entry = pick_entry(written)

    pkgs = read_index(fs)
    rec = _find(pkgs, name)
    if rec is not None:
        for f in rec['files']:
            if f not in written:
                try:
                    fs.delete(f)
                except Exception:
                    pass
        rec['entry'] = entry
        rec['files'] = written
    else:
        pkgs.append({'name': name, 'entry': entry, 'files': written})
    write_index(fs, pkgs)

    msg = '已安装 ' + name + ' (' + str(len(written)) + ' 个文件)'
    if entry:
        msg = msg + ', 入口: ' + entry + ' -> 用 run ' + name + ' 运行'
    return msg


def uninstall(fs, name):
    pkgs = read_index(fs)
    rec = _find(pkgs, name)
    if rec is None:
        return '未安装的包: ' + name
    for f in rec['files']:
        try:
            fs.delete(f)
        except Exception:
            pass
    rest = [p for p in pkgs if p['name'] != name]
    write_index(fs, rest)
    return '已卸载: ' + name


def app_source(fs, name):
    """取回已安装应用的入口源码, 供 exec 执行"""
    rec = _find(read_index(fs), name)
    if rec is None:
        return None
    if not rec['entry']:
        return None
    return fs.read(rec['entry'])


def list_packages(fs):
    lines = []
    names = [k for k in FROZEN_PKGS]
    names.sort()
    for k in names:
        lines.append('  [内嵌] ' + k + ' - ' + FROZEN_PKGS[k][0])
    for rec in read_index(fs):
        tail = ', 入口 ' + rec['entry'] if rec['entry'] else ' (无入口)'
        lines.append('  [已装] ' + rec['name'] + ' - ' +
                     str(len(rec['files'])) + ' 个文件' + tail)
    if not lines:
        return '没有可用的包'
    return '\n'.join(lines)

# ====================================================================
# from kernel/fs_kernel.py
# ====================================================================
# ============================================================
# fs_kernel.py - FSOS 内核文件区实现 (基于 krn 模块)
#
# 内核文件区由 C 侧 filesys.c 提供真实目录。Better Terminal 直接通过
# krn.list_files()/del_file() 访问它，从而与 C 文件管理器、编辑器共享同一批文件。
# 当前 FSOS 文件区是扁平命名空间；单文件上限由 krn_bridge/文件系统统一决定。
# ============================================================

import krn


class KrnFS(object):
    name = 'krn'

    # ---- 路径: 内核是扁平命名空间, 原样返回 ----
    def path(self, name):
        return name

    def cwd(self):
        return '/'

    def chdir(self, name):
        return '内核文件区没有目录概念, 不支持 cd'

    def mkdir(self, name):
        return '内核文件区没有目录概念, 不支持 mkdir'

    def isdir(self, name):
        return False

    # ---- 文件 ----
    def exists(self, name):
        try:
            krn.read_file(name)
            return True
        except Exception:
            return False

    def read(self, name):
        return krn.read_file(name)

    def write(self, name, text):
        return krn.write_file(name, text)

    def delete(self, name):
        result = krn.del_file(name)
        if isinstance(result, str) and result.startswith('ERROR'):
            raise OSError(result)
        return result

    def names(self):
        """列出内核文件区真实存在的文件。

        旧实现只读取 INDEX.TXT，导致 C 文件管理器/编辑器创建的文件在
        Better Terminal 里不可见。krn 已经提供 list_files()，这里直接复用
        内核目录作为唯一事实来源。
        """
        try:
            return list(krn.list_files())
        except Exception:
            return []

# ====================================================================
# from kernel/backend_kernel.py
# ====================================================================
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

# ====================================================================
# from core/ptos.py
# ====================================================================
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
# 注意: 下面两行在 tools/bundle_pt.py 拼合单文件时会被剔除, 因为届时
# run_cmd_text / cmd_history 已是同一文件里的全局名。因此只能"导入名字",
# 不能"导入模块后再取属性" —— 后者在拼合后会找不到模块。

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

# ====================================================================
# from core/commands.py
# ====================================================================
# ============================================================
# commands.py - 命令解析与分发
# 由 _upstream/commands.py 改造: 保留原有命令集合, 新增包管理命令,
# 并把 auto-output 的别名统一为小写 (原实现只认 Auto-output)。
# ============================================================

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
        return '用法: theme <' + '|'.join(available_themes()) + '>'
    # 设定主题 (config.theme 会被 host/theme.get_theme 优先使用)
    try:
        ok = set_theme(a)
    except ImportError:
        # 内核版拼合时不含 host 模块: 退回 config 上记一个标记, 但无 UI 可换肤
        try:
            config.theme = a
            ok = True
        except ImportError:
            ok = False
    if not ok:
        return '主题切换失败'
    # 若宿主机 UI 已启动, 立即重新套用样式 (通过 host/ui 暴露的回调)
    try:
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

# ====================================================================
# from kernel/main_kernel.py
# ====================================================================
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

# ====================================================================
# override from kernel/help_ascii.py (内核 ASCII 终端)
# ====================================================================
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
    'run <pkg|file>             run app/script/executable (.py/.sh/.elf/... )',
    '---- system ----',
    'whoami                     current user and role',
    'drivers                    list registered kernel drivers',
    'hostname [name]            show or set hostname',
    'cat <file>                 print a file from filestore',
    'reboot                     reboot the system',
    'cls                        alias of clear',
]

# 内核入口: 由打包脚本追加 (main_kernel.py 自身不自动运行)
run()
