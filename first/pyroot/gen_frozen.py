# gen_frozen.py - 把 bt.py 转换为 C 字符串源 (gen_frozen_fsos.c)
# 由构建脚本在编译前调用; 也可手动运行: python pyroot/gen_frozen.py
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "pyroot", "bt.py")
OUT = os.path.join(ROOT, "micropython", "ports", "fsos", "gen_frozen_fsos.c")


def c_escape(s: str) -> str:
    # 关键: 全部字符(含可打印 ASCII)一律编码为 \xHH 转义。
    # 绝不能把 ASCII 可打印字符原样保留, 否则若它恰是十六进制字符
    # (0-9 a-f A-F) 且紧跟在某个非 ASCII 字节的 \xNN 之后, C 编译器会把
    # "\xNN<hex字面量>" 合并读成一个更长的十六进制转义 (值越界报
    # "hex escape sequence out of range"), 吞掉该字符导致内嵌源码错乱,
    # 内核里 MP 编译时报 SyntaxError (终端一直 "Loading terminal..." 起不来)。
    # 全转义后相邻序列总以 '\' 分隔, 永不发生合并。
    out = []
    for ch in s:
        for b in ch.encode('utf-8'):
            out.append('\\x%02x' % b)
    return ''.join(out)


def _self_check(body: str, src: str) -> None:
    # 模拟 C 词法解码 \xHH 序列, 确认还原后与源文件一致
    import re
    parts = re.findall(r'\\x([0-9a-fA-F]{2})', body)
    if len(parts) * 4 != len(body):
        raise SystemExit('gen_frozen 自检失败: body 含非 \\xHH 序列')
    dec = bytes(int(p, 16) for p in parts).decode('utf-8')
    if dec != src:
        raise SystemExit('gen_frozen 自检失败: 还原内容与 bt.py 不一致')
    print('  [ok] gen_frozen 转义自检通过')


def main():
    with open(SRC, 'r', encoding='utf-8') as f:
        src = f.read()
    body = c_escape(src)
    _self_check(body, src)
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write('// 由 pyroot/gen_frozen.py 自动生成, 请勿手工编辑\n')
        f.write('#include <stdint.h>\n\n')
        f.write('// 内嵌的 Better terminal Python 源码 (UTF-8)\n')
        f.write('const char bt_py_source[] = "')
        f.write(body)
        f.write('";\n')
    print("wrote", OUT, "(%d bytes source)" % len(src))


if __name__ == "__main__":
    main()
