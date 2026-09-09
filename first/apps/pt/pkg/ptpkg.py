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
#   内核的 krn 没有列目录接口, 这个索引同时充当 pt 侧的文件清单。
#
# 内核限制 (来自 krn_bridge.c 的文件区): 单文件 4KB、最多 16 个文件、
#   文件名 24 字符、内容以首个 NUL 结尾 (因此只能存文本)。
# ============================================================
from pkg.b64 import b64decode, b64encode
from pkg.tinyunzip import read_zip
from pkg.frozen_pkgs import FROZEN_PKGS

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
