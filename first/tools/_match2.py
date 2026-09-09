# 临时: 精确坐标处逐字匹配 (定位: 逻辑正文(18,38) -> 屏幕(32+3x, 84+3y))
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
# ASCII 8x8
t8=open('boot/uefi/font8x8.h','r',encoding='utf-8').read()
i8=t8.index('g_font8x8')
g8=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{2}',t8[i8:])][:96*8]
G8=[g8[i*8:(i+1)*8] for i in range(96)]

def cell_cp(lx,ly,cp):
    """逻辑(lx,ly) 12x12 格; 每格3px 采样中值"""
    vals=[]
    for r in range(12):
        for c in range(12):
            exp=0
            if cp>=0x80:
                i=CP.get(cp); exp=0 if i is None else ((BM[i][r]>>(11-c))&1)
            else:
                if 0x20<=cp<0x80:
                    gl=G8[cp-0x20]
                    # 8x8 列优先: 每字节一列 bit0=最上, 画在 y+2
                    if c<8 and r>=2 and r<10:
                        exp=(gl[c]>>(r-2))&1
            sx=OX+lx*SC; sy=OY+ly*SC
            v=im.getpixel((sx+c*SC+1, sy+r*SC+1))
            got = v<170
            vals.append((exp,got))
    on=sum(1 for e,g in vals if e==g)
    return on/len(vals)

# 从正文 (lx,ly)=(18,38) 起逐字, 每字12宽(汉字)或8(ASCII)
def match_line(ly, chars, x0):
    x=x0
    res=[]
    for ch in chars:
        cp=ord(ch)
        w=12 if cp>=0x80 else 8
        s=cell_cp(x,ly,cp)
        res.append((ch,s))
        x+=w
    return res

print('== 首行 y=38 ==')
r=match_line(38,'自由安全操作系统 FSOS',18)
print(' '.join('%s=%.2f'%(c,v) for c,v in r))
print('== 各预期行: 找每行 ly (d_about 每行间距16, 起点38/54/70/86/102) ==')
for i,(ly,txt) in enumerate([(38,'自由安全操作系统 FSOS'),(54,'Windows 风格中文桌面'),
    (70,'双击图标打开窗口'),(86,'左下角开始菜单启动应用'),(102,'ESC 退出桌面  F1 快捷键')]):
    r=match_line(ly,txt,18)
    avg=sum(v for _,v in r)/len(r)
    print('ly=%d avg=%.2f  %s'%(ly,avg,' '.join('%s=%.2f'%(c,v) for c,v in r)))
