h)
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
        