# -*- coding: utf-8 -*-
# 临时诊断脚本: 扫 pyroot/bt.py 中 MP 1.22 可能不支持的语法
import re

PATH = r'e:\project\clion\project_system\first\pyroot\bt.py'
with open(PATH, 'r', encoding='utf-8') as f:
    lines = f.read().split('\n')

def show(ln, i, tag, line):
    if i >= len(lines): return
    print('L%d %s: %r' % (ln, tag, line.strip()[:120]))

print('total lines:', len(lines))
for i, line in enumerate(lines, 1):
    s = line.rstrip()
    if not s.strip() or s.strip().startswith('#'):
        continue
    # 行首关键字（去注释/字符串粗查）
    st = s.lstrip()
    m = re.match(r'\b(match|case)\b', st)
    if m and re.search(r':\s*$', st):
        show(i, 0, 'MATCH/CASE', s); continue
    if re.search(r':=', s):
        show(i, 1, 'WALRUS', s); continue
    if re.search(r'\braise\b.*\bfrom\b', s):
        show(i, 2, 'RAISE-FROM', s); continue
    # 参数/返回注解粗查: def ... (:type 或 -> 在 def 行)
    if re.match(r'\s*def\s+\w+', s) and re.search(r'(->|:[^=])', s):
        show(i, 3, 'ANNOT-DEF', s); continue
    # f-string
    if re.search(r'f[\'"]', s):
        show(i, 4, 'FSTRING', s); continue
    if re.search(r'\bnonlocal\b', s):
        show(i, 5, 'NONLOCAL', s); continue
    if re.search(r'^\s*async\s+', s):
        show(i, 6, 'ASYNC', s); continue
