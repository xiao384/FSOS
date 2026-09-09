# 临时: 定位 UI5 窗口: 输出网格亮度图找出窗口区域
from PIL import Image
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
W,H=im.size
px=im.load()
# 8px 一格, 输出 128x96 字符图, 深色窗口边框会体现
def ch(v):
    return '#' if v<60 else ('+' if v<140 else ('.' if v<210 else ' '))
out=[]
for y in range(0,H,8):
    row=''
    for x in range(0,W,8):
        row+=ch(px[x,y])
    out.append(row)
open('output/ui5_map.txt','w').write('\n'.join(out))
print('done', W, H)
