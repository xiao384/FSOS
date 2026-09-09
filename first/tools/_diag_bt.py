# -*- coding: utf-8 -*-
# 临时诊断脚本(用完删除): 提取 gen_frozen_fsos.c 内嵌 BT 源码并对比 pyroot/bt.py
import re, sys, hashlib, os

CFILE = r'e:\project\clion\project_system\first\micropython\ports\fsos\gen_frozen_fsos.c'
PYFILE = r'e:\project\clion\project_system\first\pyroot\bt.py'

with open(CFILE, 'r', encoding='utf-8', errors='replace') as f:
    ctext = f.read()

print('gen_frozen_fsos.c size:', len(ctext))
# 打印头部 400 字符看结构
print('HEAD>>>', ctext[:400].replace('\n', '\\n'))
print('TAIL>>>', ctext[-200:].replace('\n', '\\n'))

# 尝试方式1: 字符串字面量 "..." (可能含 \xHH 转义)
def unescape(s):
    out = []
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '\\' and i + 1 < n:
            d = s[i+1]
            if d == 'x' and i + 3 < n:
                try:
                    out.append(chr(int(s[i+2:i+4], 16)))
                    i += 4
                    continue
                except ValueError:
                    pass
            mp = {'n':'\n','t':'\t','r':'\r','0':'\0','\\':'\\','"':'"',"'":"'"}
            if d in mp:
                out.append(mp[d]); i += 2; continue
            out.append('\\'); i += 1; continue
        out.append(c); i += 1
    return ''.join(out)

m = re.search(r'"((?:[^"\\]|\\.)*)"', ctext)
if m:
    raw = unescape(m.group(1))
    # raw 是按 \xHH 还原后的 latin-1 风格字符流 -> 还原成 utf-8 字节再解码
    try:
        src = raw.encode('latin-1').decode('utf-8')
        print('extracted from quoted literal (utf-8 decoded), len=', len(src))
    except UnicodeDecodeError as e:
        print('utf-8 decode error:', e)
        src = raw
        print('fallback raw len=', len(raw))
else:
    # 方式2: 数字数组 { 'a', 'b', ... } 或 {0x61,...}
    arr = re.findall(r"'([^'])'|0x([0-9a-fA-F]{2})|\b(\d{1,3})\b", ctext)
    if arr:
        bs = []
        for a,b,d in arr:
            if a: bs.append(a)
            elif b: bs.append(chr(int(b,16)))
            else: bs.append(chr(int(d)))
        src = ''.join(bs)
        print('extracted from numeric array, len=', len(src))
    else:
        print('FAILED to extract'); sys.exit(1)

with open(PYFILE, 'r', encoding='utf-8') as f:
    py = f.read()
print('pyroot/bt.py size:', len(py))
print('md5 embedded:', hashlib.md5(src.encode('utf-8')).hexdigest())
print('md5 pyroot  :', hashlib.md5(py.encode('utf-8')).hexdigest())

if src == py:
    print('RESULT: IDENTICAL -> 内嵌源码与 pyroot/bt.py 一致')
else:
    print('RESULT: DIFFERENT')
    # 找首个差异
    for i,(a,b) in enumerate(zip(src,py)):
        if a != b:
            print('first diff at', i, repr(src[max(0,i-40):i+40]), '|||', repr(py[max(0,i-40):i+40]))
            break
    else:
        print('one is prefix of other; len diff', len(src)-len(py))
        if len(src) > len(py):
            print('embedded tail:', repr(src[len(py):len(py)+120]))
        else:
            print('pyroot tail  :', repr(py[len(src):len(src)+120]))
