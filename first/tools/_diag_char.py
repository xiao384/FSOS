# -*- coding: utf-8 -*-
import re
BT = r'e:\project\clion\project_system\first\pyroot\bt.py'
lines = open(BT, 'r', encoding='utf-8').read().split('\n')
# seg4 == inflate 段: host seg3 (line 261..461) -> 用 mark 精确定位
marks = []
for i, l in enumerate(lines):
    if l.startswith('# from '):
        marks.append((i, l))
start = end = None
for i, (ln, name) in enumerate(marks):
    nxt = marks[i + 1][0] if i + 1 < len(marks) else len(lines)
    if 'inflate' in name:
        start, end = ln, nxt
        break
print('inflate seg lines %d..%d' % (start, end))
seg = lines[start:end]

allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ \t\n#()[]{}'\":,.;+-*/%<>=!&|^~@\\")
for i, l in enumerate(seg, start):
    for ch in l:
        if ch not in allowed and ord(ch) >= 32:
            # 忽略注释/字符串内部? 仍打印候选
            print('L%d char U+%04X %r ctx: ...%s...' % (i, ord(ch), ch, l[max(0, l.index(ch)-25):l.index(ch)+25]))
# 也检查 docstring 内是否用了 \u 等 (tokenize 精确)
import tokenize, io
text = '\n'.join(seg)
for t in tokenize.generate_tokens(io.StringIO(text).readline):
    if t.type == tokenize.STRING:
        if '\\' in t.string:
            for m in re.finditer(r'\\(.)', t.string):
                if m.group(1) not in 'nrt0\\"\'xX':
                    print('L%d odd escape \\%s in %r' % (t.start[0], m.group(1), t.string[:60]))
print('scan done')
