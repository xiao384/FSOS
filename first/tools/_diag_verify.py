# -*- coding: utf-8 -*-
import os, re
BT = r'e:\project\clion\project_system\first\pyroot\bt.py'
CF = r'e:\project\clion\project_system\first\micropython\ports\fsos\gen_frozen_fsos.c'
b = open(BT, 'rb').read()
print('bt.py bytes:', len(b), ' mtime:', os.path.getmtime(BT))
print('gen_frozen_fsos.c mtime:', os.path.getmtime(CF))
# 真机 SEGDIAG: 13 segs, 71425 bytes。比较 strlen 应与 C 数组中字节数一致
m = re.search(r'bt_py_source\[\]\s*=\s*"((?:[^"\\]|\\.)*)"', open(CF, encoding='utf-8', errors='replace').read())
raw = m.group(1)
# 估算 C 词法解出的字节数: \xHH => 1 byte
n = len(re.findall(r'\\x[0-9a-fA-F]{2}', raw))
print('C array \\xHH tokens:', n, ' => embedded byte len ~', n)
# bt.py 内 "# from " 的字节偏移
import io
text = open(BT, 'r', encoding='utf-8').read()
off = 0
segs = []
for i, line in enumerate(text.split('\n'), 1):
    if line.startswith('# from '):
        segs.append((i, off))
    off += len(line.encode('utf-8')) + 1
print('segments (# from) with byte offsets:')
for i, (ln, o) in enumerate(segs):
    nxt = segs[i+1][1] if i+1 < len(segs) else len(b)
    if 'inflate' in segs[i][0] or i < 4:
        print('  seg%d: line %d byte %d..%d len %d <== %s' % (i, ln, o, nxt, nxt-o, segs[i][1].split('from ')[1]))
# 比对 C 数组解出的字节流与 bt.py 前若干前缀
src = open(BT,'r',encoding='utf-8').read()
emb = bytes(int(x,16) for x in re.findall(r'\\x([0-9a-fA-F]{2})', raw))
try:
    print('embedded decodes utf8:', emb.decode('utf-8') == src)
except Exception as e:
    print('utf8 decode fail', e)
print('first 60 embedded bytes vs bt.py head equal:', emb[:60] == b[:60])
# inflate 段对齐: 真机 seg4 [8694..15126)
if len(segs) >= 5:
    infl = [s for s in segs if 'inflate' in s[1]][0]
    print('host inflate seg:', infl[0], infl[1], ' len=', len(b) - infl[1] if infl is segs[-1] else (segs[segs.index(infl)+1][1]-infl[1]))
print('embedded total==bt bytes:', len(emb)==len(b))
