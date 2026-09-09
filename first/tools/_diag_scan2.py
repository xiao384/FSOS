# -*- coding: utf-8 -*-
# 临时诊断: 扫 pyroot/bt.py 所有 MP1.22-MINIMUM 违规语法
#   1) f-string  (MICROPY_PY_FSTRINGS=0)
#   2) str 字面量内 \uXXXX / \UXXXXXXXX 转义 (STR_UNICODE=0, 值>=0x100 -> SyntaxError)
#   3) bytes 字面量内 \u 是安全的(b'\\u..'), 跳过
import re

PATH = r'e:\project\clion\project_system\first\pyroot\bt.py'
with open(PATH, 'r', encoding='utf-8') as f:
    text = f.read()
lines = text.split('\n')

fstr_pat = re.compile(r'(?<![0-9A-Za-z_])[rRbB]*[fF](?=[\'"])|(?<![0-9A-Za-z_])[fF][rRbB]*(?=[\'"])')
# 去掉注释后扫描比较干净: 只保留字符串? 简化用逐行去注释
def strip_comment(line):
    # 粗略: 忽略在引号内的#, 这里够用
    in_s = in_d = False
    out = []
    i = 0
    while i < len(line):
        c = line[i]
        if c == '\\':
            out.append(line[i:i+2]); i += 2; continue
        if c == "'" and not in_d: in_s = not in_s
        elif c == '"' and not in_s: in_d = not in_d
        elif c == '#' and not in_s and not in_d:
            break
        out.append(c); i += 1
    return ''.join(out)

n_fs = 0
print('== f-string hits ==')
for i, line in enumerate(lines, 1):
    s = strip_comment(line)
    if fstr_pat.search(s):
        print('L%d: %r' % (i, line.strip()[:130])); n_fs += 1
print('total f-string hits:', n_fs)

print()
print('== \\uXXXX / \\UXXXXXXXX escapes in string literals ==')
u_pat = re.compile(r'\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})')
import tokenize, io
# 用 CPython tokenize 提取真实字符串token(排除注释), 判断每个转义所在 token 前缀是否 bytes
try:
    toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    for t in toks:
        if t.type == tokenize.STRING and t.string[0] not in 'bB':
            for mm in u_pat.finditer(t.string):
                hexs = mm.group(1) or mm.group(2)
                val = int(hexs, 16)
                if val >= 0x100:
                    ln = t.start[0]
                    print('L%d (str token %.60r): escape \\%s value 0x%04X' % (ln, t.string, 'u' if mm.group(1) else 'U', val))
except Exception as e:
    import traceback; traceback.print_exc()
print('done')
