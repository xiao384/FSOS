# -*- coding: utf-8 -*-
import os, re
CF = r'e:\project\clion\project_system\first\micropython\ports\fsos\gen_frozen_fsos.c'
c = open(CF, 'rb').read()
print('C file size:', len(c))
print('C file head:', c[:120])
# 统计整个文件 \x
print('all \\x in file:', len(re.findall(rb'\\x[0-9a-fA-F]{2}', c)))
# 找到 bt_py_source 赋值
mm = re.search(rb'bt_py_source\[\]\s*=\s*"((?:[^"\\]|\\.)*)"', c)
print('bt_py_source literal found:', bool(mm))
if mm:
    body = mm.group(1)
    print('literal bytes:', len(body), ' xHH count:', len(re.findall(rb'\\x[0-9a-fA-F]{2}', body)))
    # 非 xHH 序列部分
    nonx = re.sub(rb'\\x[0-9a-fA-F]{2}', b'', body)
    print('non-\\x parts:', nonx[:200])
# 第二行往后有无其它数组
print('C file tail:', c[-200:])
