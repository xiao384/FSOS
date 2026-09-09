# -*- coding: utf-8 -*-
import re, collections
p = r'apps\pt\pkg\inflate.py'
b = open(p, 'rb').read()
print('size:', len(b))
print('first bytes:', b[:8].hex())
print('CR count:', b.count(b'\r'))
print('NUL count:', b.count(b'\x00'))
weird = collections.Counter()
ctrl = collections.defaultdict(list)
for i, ch in enumerate(b):
    if ch >= 0x80:
        weird[ch] += 1
    if ch < 32 and ch not in (9, 10, 13):
        ctrl[ch].append(i)
print('high byte counts:', weird.most_common(12))
print('ctrl positions:', {hex(k): v[:8] for k, v in ctrl.items()})
# 用 tokenize 找所有字符串/注释外的非 ASCII
import tokenize, io
s = b.decode('utf-8')
for t in tokenize.generate_tokens(io.StringIO(s).readline):
    if t.type == tokenize.NAME and any(ord(c) > 127 for c in t.string):
        print('non-ascii NAME', repr(t.string), 'L%d' % t.start[0])
    if t.type == tokenize.OP and any(ord(c) > 127 for c in t.string):
        print('non-ascii OP', repr(t.string), 'L%d' % t.start[0])
print('ok')
