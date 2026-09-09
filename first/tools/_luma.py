# 临时: 探测 UI5 文字区亮度分布, 确认前景/背景阈值
from PIL import Image
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
# 首字"像"约屏幕 (86..86+36, 198..198+36)
sx,sy=86,198
import collections
vals=[]
for y in range(sy,sy+36):
    for x in range(sx,sx+36):
        vals.append(im.getpixel((x,y)))
c=collections.Counter(v//16*16 for v in vals)
print('histogram (bucket:count) for first glyph cell area:')
for k in sorted(c):
    print(' %3d-%3d: %d'%(k,k+15,c[k]))
print('min',min(vals),'max',max(vals),'median',sorted(vals)[len(vals)//2])
