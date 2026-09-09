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
from pkg.inflate import inflate

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
