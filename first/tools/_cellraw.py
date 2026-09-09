# 临时: 打印 UI5 首字格原始亮度 12x12 矩阵 与 字库"像"字形
from PIL import Image
import re
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
SC=3
# 每个逻辑格像素取 3x3 块中心; 无阈值, 打印数值 0-63
sx,sy=86,198
print('== UI5 raw luminance, first 12x12 logical cell @(86,198) ==')
for r in range(12):
    row=[]
    for c in range(12):
        v=im.getpixel((sx+c*SC+1, sy+r*SC+1))
        row.append('%2d'%v)
    print(' '.join(row))

text=open('user/cjk_font.h','r',encoding='utf-8').read()
cps=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
    re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}',text).group(1))]
m=re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};',text,re.S)
arr=[]
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',line))
BM=[arr[i*12:(i+1)*12] for i in range(len(cps))]
CP={cp:i for i,cp in enumerate(cps)}
def glyph_ascii(cp):
    i=CP.get(cp)
    if i is None: return '(absent)'
    return '\n'.join(''.join('#' if (BM[i][r]>>(11-c))&1 else '.' for c in range(12)) for r in range(12))
for ch in '自由安全操作系统':
    print('== font %s U+%04X =='%(ch,ord(ch)))
    print(glyph_ascii(ord(ch)))
