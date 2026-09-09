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
