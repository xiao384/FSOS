# -*- coding: utf-8 -*-
# 临时诊断: 定位 pyroot/bt.py 中 inflate 段边界与内容, 与源文件对比
import re

BT = r'e:\project\clion\project_system\first\pyroot\bt.py'
SRC = r'e:\project\clion\project_system\first\apps\pt\pkg\inflate.py'

with open(BT, 'r', encoding='utf-8') as f:
    blines = f.read().split('\n')

# 收集 "# from xxx" 段起点(行号,0基)
marks = []
for i, l in enumerate(blines):
    if l.startswith('# from '):
        marks.append((i, l[8:].strip()))
print('segment marks:')
for i, (ln, name) in enumerate(marks):
    end = marks[i + 1][0] if i + 1 < len(marks) else len(blines)
    print('  seg%d: bt.py line %d..%d  <== %s' % (i, ln, end - 1, name))

# 找到 inflate 段
idx = None
for i, (ln, name) in enumerate(marks):
    if 'inflate' in name:
        idx = i
        break
assert idx is not None, 'inflate not found'
start_ln = marks[idx][0]
end_ln = marks[idx + 1][0] if idx + 1 < len(marks) else len(blines)
seg_text = '\n'.join(blines[start_ln:end_ln])

with open(SRC, 'r', encoding='utf-8') as f:
    src_text = f.read()

print()
print('bt.py inflate seg: line %d..%d, %d bytes' % (start_ln, end_ln, len(seg_text.encode('utf-8'))))
print('source inflate.py: %d bytes' % len(src_text.encode('utf-8')))

# 比对: 源文件内容是否作为子串出现在 seg 中 (除 bundle 头与 import 删除)
# bundle 每段由 header(# ===/# from/# ===) + body组成, body=去 import 行
import difflib
sim = difflib.SequenceMatcher(None, src_text.split('\n'), seg_text.split('\n'))
print('ratio src vs seg:', round(sim.ratio(), 3))
for tag, i1, i2, j1, j2 in sim.get_opcodes():
    if tag == 'equal':
        continue
    print('  %s src[%d:%d] seg[%d:%d]' % (tag, i1, i2, j1, j2))
    if tag in ('delete', 'replace'):
        print('    src lines:', src_text.split('\n')[i1:i2][:6])
    if tag in ('insert', 'replace'):
        print('    seg lines:', seg_text.split('\n')[j1:j2][:6])
