# 临时: 决定性验证 --- 从 UI5 截图首行提取 4 汉字像素, 与字库数据渲染比对
from PIL import Image
import re

# 1) 字库数据渲染 "自由安全操作系统" 每个字 -> 12x12 0/1 grid
text = open('user/cjk_font.h','r',encoding='utf-8').read()
cps = [int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
      re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
arr=[]
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
BM=[arr[i*12:(i+1)*12] for i in range(len(cps))]
LUT={cp:BM[i] for i,cp in enumerate(cps)}
def fontgrid(cp):
    gl=LUT.get(cp)
    g=[]
    for r in range(12):
        bits=gl[r] if gl else 0
        g.append([1 if (bits>>(11-c))&1 else 0 for c in range(12)])
    return g
chars='自由安全操作系统'

# 2) 从 UI5 首行找 4 个汉字像素 (需要先定位首行)
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
W,H=im.size
px=im.load()
# 首行 "自由安全操作系统" @逻辑(18,38) scale3 + off(32,84) -> (86,198); 每字36px
# 但窗口可能被拖动。我们扫描: 找 y 在200-260 之间的一行深色文本带
# 简化: 裁剪 (85,230)-(230,275) 区域 (对应第一行4字), 做 ASCII
# 定位: 直接按 grep 式 —— 打印该区域每 3px 一格
x0,y0 = 84, 238
x1,y1 = 84+4*36+8, 238+36  # 4字, 每字36px, 留边
print('UI5 sample rect', (x0,y0,x1,y1))
for yy in range(y0, min(y1,H), 3):
    row=''
    for xx in range(x0, min(x1,W), 3):
        v=px[xx,yy]
        row += '#' if v<110 else ('.' if v>200 else ':')
    print(row)
print('=== font data "自由安全操作系统" (12x12 each) ===')
for cp in chars:
    g=fontgrid(ord(cp))
    print('cp %04X'%ord(cp))
    for r in g:
        print(''.join('#' if b else '.' for b in r))
