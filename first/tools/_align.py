# 临时: 预期位图 vs 截图实际位图, 带对齐搜索, 统计匹配率
from PIL import Image
import re
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
SC=3; OX,OY=32,84

text=open('user/cjk_font.h','r',encoding='utf-8').read()
cps=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
    re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}',text).group(1))]
m=re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};',text,re.S)
arr=[]
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',line))
BM=[arr[i*12:(i+1)*12] for i in range(len(cps))]
CP={cp:i for i,cp in enumerate(cps)}
t8=open('boot/uefi/font8x8.h','r',encoding='utf-8').read()
i8=t8.index('g_font8x8')
g8=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{2}',t8[i8:])][:96*8]
G8=[g8[i*8:(i+1)*8] for i in range(96)]

def exp_pt(x,y,ch):
    """逻辑坐标处该字符是否笔画。返回0/1/None(字符外)"""
    cp=ord(ch)
    if cp>=0x80:
        i=CP.get(cp)
        if i is None: return None
        if x<0 or x>=12 or y<0 or y>=12: return None
        return 1 if ((BM[i][y]>>(11-x))&1) else 0
    else:  # ASCII 8x8 高8, +2 偏移
        if cp<0x20 or cp>=0x80: return None
        if x<0 or x>=8 or y<2 or y>=10: return None
        gl=G8[cp-0x20]
        return 1 if ((gl[x]>>(y-2))&1) else 0

# 用第2行验证 (更多汉字更可靠): 从 (18,54) 起画 "Windows 风格中文桌面"
def build_expect(ly, s, x0):
    """返回 (x,y)->0/1 dict in logical"""
    out={}
    x=x0
    for ch in s:
        cp=ord(ch)
        for dy in range(12):
            for dx in range(12):
                v=exp_pt(dx,dy,ch)
                if v is not None:
                    out[(x+dx,ly+dy)]=v
        x += 12 if cp>=0x80 else 8
    return out

def compare(ly,s,x0, dx0=0,dy0=0):
    exp=build_expect(ly,s,x0)
    tot=0; good=0
    for (lx,lyy),e in exp.items():
        sx=OX+lx*SC+1+dx0; sy=OY+lyy*SC+1+dy0
        if sx<0 or sy<0 or sx>=1024 or sy>=768: continue
        v=im.getpixel((sx,sy)); got=1 if v<170 else 0
        tot+=1
        if got==e: good+=1
    return good/tot if tot else 0

for ly,s in [(38,'自由安全操作系统 FSOS'),(54,'Windows 风格中文桌面'),(70,'双击图标打开窗口'),
             (86,'左下角开始菜单启动应用'),(102,'ESC 退出桌面  F1 快捷键')]:
    best=(0,0,0)
    for dy0 in range(-4,5):
        for dx0 in range(-4,5):
            sc=compare(ly,s,18,dx0,dy0)
            if sc>best[0]: best=(sc,dx0,dy0)
    print('ly=%d best=%.3f dx=%d dy=%d  %s'%(ly,best[0],best[1],best[2],s))
