# -*- coding: utf-8 -*-
# 临时诊断: 列出 bt.py 全部 AST 节点类型(带首见行), 找 MP1.22-MINIMUM 不支持者
import ast, collections

PATH = r'e:\project\clion\project_system\first\pyroot\bt.py'
with open(PATH, 'r', encoding='utf-8') as f:
    src = f.read()
tree = ast.parse(src)
seen = {}
for node in ast.walk(tree):
    t = type(node).__name__
    if t not in seen:
        seen[t] = getattr(node, 'lineno', '?')
for t, ln in sorted(seen.items(), key=lambda kv: kv[1] if isinstance(kv[1], int) else 0):
    print('%-18s first@L%s' % (t, ln))

# 高亮 MP 1.22 可疑节点并打印所在行原文
susp = {'AnnAssign', 'JoinedStr', 'NamedExpr', 'Match', 'MatchValue', 'DictComp', 'SetComp',
        'Starred', 'AsyncFunctionDef', 'AsyncFor', 'AsyncWith', 'Constant', 'TryStar'}
lines = src.split('\n')
print()
print('== suspicious detail ==')
for node in ast.walk(tree):
    t = type(node).__name__
    if t in {'AnnAssign', 'JoinedStr', 'NamedExpr', 'Match', 'MatchValue', 'TryStar'}:
        ln = getattr(node, 'lineno', 0)
        print('%s @L%d: %s' % (t, ln, (lines[ln-1] if 0 < ln <= len(lines) else '').strip()[:130]))
# Starred 是否用于 unpack(ok) vs 字面量内展开(需查)
for node in ast.walk(tree):
    if isinstance(node, ast.Starred) and isinstance(node.value, (ast.List, ast.Tuple, ast.Set, ast.Dict)):
        ln = node.lineno
        print('Starred-in-literal @L%d: %s' % (ln, lines[ln-1].strip()[:130]))
