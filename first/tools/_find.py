# 临时: 全局模板搜索 "像" 的位置; 再在该行验证其余字
from PIL import Image
import re
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
SC=3
text=open('user/cjk_font.h','r',encoding='utf-8').read()
cps=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
    re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}',text).group(1))]
m=re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};',text,re.S)
arr=[]
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',line))
BM=[arr[i*12:(i+1)*12] for i in range(len(cps))]
CP={cp:i for i,cp in enumerate(cps)}
def glyph(cp):
    i=CP.get(cp)
    if i is None: return None
    return [[ (BM[i][r]>>(11-c))&1 for c in range(12)] for r in range(12)]

def scr_glyph_pts(sx,sy,cp):
    g=glyph(cp)
    if g is None: return None
    pts=[]
    for r in range(12):
        for c in range(12):
            if g[r][c]:
                pts.append((sx+c*SC+1, sy+r*SC+1))
    return pts

def score(sx,sy,cp, dark=170):
    pts=scr_glyph_pts(sx,sy,cp)
    if not pts: return 0
    on=sum(1 for x,y in pts if im.getpixel((x,y))<dark)
    return on/len(pts)

# 全图搜 像 的最佳位置 (fg 覆盖率最高, 且邻域也有结构)
best=[]
for sy in range(80,600,1):
    for sx in range(40,700,1):
        s=score(sx,sy,ord('像'))
        if s>0.5:
            best.append((s,sx,sy))
best.sort(reverse=True)
print('top candidates for 像:')
for s,sx,sy in best[:10]:
    print('  score=%.3f sx=%d sy=%d'%(s,sx,sy))
