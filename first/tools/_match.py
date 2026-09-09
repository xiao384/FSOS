# 临时: 决定性验证 --- 从 UI5 截图提取正文首行字格, 与字库全库匹配, 看是否=预期字
from PIL import Image
import re

im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
W,H=im.size

# 字库
text=open('user/cjk_font.h','r',encoding='utf-8').read()
cps=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
    re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}',text).group(1))]
m=re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};',text,re.S)
arr=[]
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',line))
BM=[arr[i*12:(i+1)*12] for i in range(len(cps))]
CP={cp:i for i,cp in enumerate(cps)}
def bit(cp,r,c):
    i=CP.get(cp)
    if i is None: return 0
    return (BM[i][r]>>(11-c))&1

# 预期字串 (首行: 4汉字 + 空格 + FSOS)。空格只占8px; FSOS 是 ascii
line1='自由安全操作系统 FSOS'
# 定位: 正文首行位于屏幕 y≈198..234 (12逻辑x3); 试 sx 起点 84..100
SC=3
def score_cell(sx, sy, cp):
    """提取 (sx,sy) 起的 12x12 逻辑格(每格3px), 与字库 cp 相关; 返回匹配度[0,1]"""
    on=0; tot=0
    for r in range(12):
        for c in range(12):
            exp=bit(cp,r,c)
            # 采样该逻辑像素块中心
            v=im.getpixel((sx+c*SC+1, sy+r*SC+1))
            got = v<160   # 前景暗
            tot+=1
            if exp==got: on+=1
    return on/tot

# 逐字格: 汉字 12px 宽, 空格 8, ASCII 8
# 从 sy 扫描找最优行
best_sy=None; bestscore=0
for sy in range(180,300):
    s=score_cell(80, sy, ord('像'))
    if s>bestscore: bestscore=s; best_sy=sy
print('best sy for 像 =', best_sy, 'score', bestscore)

# 用 sy=best 扫描 x, 从最左开始每格找出最优 cp (仅限高笔画区)
# 尝试 x 起点 60..140
cands=[]
for x0 in range(60,160,SC):
    ok=True; got=[]
    x=x0
    for ch in line1:
        cp=ord(ch)
        w=12 if cp>=0x80 else 8
        # 找该位置最佳匹配
        best=(-1,0)
        # 全库匹配太慢: 若预期字就在库中, 直接比较得分并找更优
        s_exp=score_cell(x,best_sy,cp)
        best=(-1,s_exp)
        got.append((ch,s_exp))
        x+=w
    sc=sum(v for _,v in got)/len(got)
    cands.append((x0,sc,got))
cands.sort(key=lambda t:-t[1])
for x0,sc,got in cands[:5]:
    print('x0=%d avg=%.3f'%(x0,sc), ' '.join('%s=%.2f'%(ch,v) for ch,v in got))
