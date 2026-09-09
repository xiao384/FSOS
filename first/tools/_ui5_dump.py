# 临时: 将 UI5 "关于"窗口客户区完整 dump 为文本
from PIL import Image
import statistics
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
W,H=im.size
X0,X1,Y0,Y1 = 60, 620, 140, 500
lines=[]
for ly in range(Y0, Y1):
    vals=[im.getpixel((x,ly)) for x in range(X0,X1)]
    # 背景 = 最常见值(中位数偏上), 文字更暗
    sv=sorted(vals)
    bg=sv[int(len(sv)*0.8)]
    fg=sv[int(len(sv)*0.05)]
    if bg-fg<12:
        continue   # 无文字
    thr=(bg+fg)//2
    # 每3px一采样(平均后阈值)
    row=[]
    for x in range(X0,X1-2,3):
        v=(vals[x-X0]+vals[x-X0+1]+vals[x-X0+2])//3
        row.append('#' if v<thr else '.')
    lines.append('y%03d %s'%(ly,''.join(row)))
open('output/ui5_window.txt','w',encoding='utf-8').write('\n'.join(lines))
print('lines', len(lines))
