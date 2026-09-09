# 临时: 并排对照 (左=UI5截图放大 / 右=字库数据渲染)
from PIL import Image, ImageDraw
import re, hashlib

# 左: UI5 截图里"关于"窗口区域放大
ui = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('RGB')
# 全图是1024x768; 窗口正文大致位于中上部。取区域后再自动找文字行
left = ui.crop((60, 205, 620, 330)).resize((560*2, 125*2), Image.NEAREST)

# 右: 用字库渲染 "自由安全操作系统 FSOS\nWindows 风格中文桌面\n双击图标打开窗口\n左下角开始菜单启动应用\nESC 退出桌面  F1 快捷键"
text = open('user/cjk_font.h','r',encoding='utf-8').read()
cps=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
     re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}',text).group(1))]
m=re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};',text,re.S)
arr=[]
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',line))
BM=[arr[i*12:(i+1)*12] for i in range(len(cps))]
LUT={cp:BM[i] for i,cp in enumerate(cps)}

t8=open('boot/uefi/font8x8.h','r',encoding='utf-8').read()
i8=t8.index('g_font8x8')
g8raw=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{2}',t8[i8:])][:96*8]
G8=[g8raw[i*8:(i+1)*8] for i in range(96)]

W,H,SC=220,90,4
right=Image.new('RGB',(W*SC,H*SC),(255,255,255))
dr=ImageDraw.Draw(right)
def setp(x,y,c): dr.rectangle([x*SC,y*SC,x*SC+SC-1,y*SC+SC-1],fill=c)
def draw(dr,x,y,s,fg):
    for ch in s:
        cp=ord(ch)
        if cp<0x80:
            gl=G8[cp-0x20]
            for col in range(8):
                bits=gl[col]
                for row in range(8):
                    if (bits>>row)&1: setp(x+col,y+2+row,fg)
            x+=8
        else:
            glb=LUT.get(cp)
            for r in range(12):
                bits=glb[r] if glb else 0
                for c in range(12):
                    if (bits>>(11-c))&1: setp(x+c,y+r,fg)
            x+=12
y=6
for ln in ['自由安全操作系统 FSOS','Windows 风格中文桌面','双击图标打开窗口','左下角开始菜单启动应用','ESC 退出桌面  F1 快捷键']:
    draw(dr,4,y,ln,(0,0,0))
    y+=17

# 并排: 上方 UI5, 下方 PC 渲染 (同文本); 中间分隔
w = max(left.width, right.width)
canvas = Image.new('RGB',(w, left.height+right.height+20),(255,0,255))
canvas.paste(left,(0,0))
canvas.paste(right,(0,left.height+20))
canvas.save(r'output\ui5_vs_pc.png')
h = hashlib.md5(open(r'output\ui5_vs_pc.png','rb').read()).hexdigest()[:8]
print('saved', canvas.size, h)
