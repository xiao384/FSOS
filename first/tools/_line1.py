# 临时: 两种对照 --- (A) 超放大 UI5 首行; (B) PC 用与 cjk.c 完全一致算法横排渲染
from PIL import Image, ImageDraw
import re

# (B1) 读入 cjk_font.h
text = open('user/cjk_font.h', 'r', encoding='utf-8').read()
cps = [int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
      re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
arr = []
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
BM = [arr[i*12:(i+1)*12] for i in range(len(cps))]
lookup = {cp:BM[i] for i,cp in enumerate(cps)}

# (B2) 读入 8x8 ASCII 字体 (font8x8.h, 列优先, 每字节一列, bit0=最上)
t8 = open('boot/uefi/font8x8.h', 'r', encoding='utf-8').read()
i8 = t8.index('g_font8x8')
g8raw = [int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{2}', t8[i8:])][:96*8]
G8 = [g8raw[i*8:(i+1)*8] for i in range(96)]

W,H,SC = 200,90,4
def make_canvas(bg):
    img = Image.new('RGB',(W*SC,H*SC),bg)
    return img, ImageDraw.Draw(img)
FG = (0x55,0x30,0x10)
BG = (0xd4,0xd4,0xd4)

def dpset(dr,x,y,c):
    dr.rectangle([x*SC,y*SC,x*SC+SC-1,y*SC+SC-1],fill=c)

def draw_text(dr, x, y, s, fg, bg):
    # s: python str, 与 cjk.c 处理 utf-8 同构(字符已解码)
    for ch in s:
        cp = ord(ch)
        if cp < 0x80:
            gl = G8[cp-0x20]
            for col in range(8):
                bits = gl[col]
                for row in range(8):
                    dpset(dr, x+col, y+2+row, fg if (bits>>row)&1 else bg)
            x += 8
        else:
            glb = lookup.get(cp)
            for r in range(12):
                for c in range(12):
                    dpset(dr, x+c, y+r, bg)
            if glb:
                for r in range(12):
                    bits = glb[r]
                    for c in range(12):
                        if (bits>>(11-c))&1:
                            dpset(dr, x+c, y+r, fg)
            x += 12

img, dr = make_canvas(BG)
y = 4
for ln in ['自由安全操作系统 FSOS','Windows 风格中文桌面','双击图标打开窗口',
           '左下角开始菜单启动应用','ESC 退出桌面  F1 快捷键']:
    draw_text(dr, 4, y, ln, FG, BG)
    y += 17
img.save(r'output\pc_line1.png')

# (A) 裁 UI5 首行
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('RGB')
print('ui5 size', im.size)
crop = im.crop((60, 210, 560, 300))
crop = crop.resize((crop.width*2, crop.height*2), Image.NEAREST)
crop.save(r'output\ui5_line1.png')
print('saved ui5_line1.png', crop.size, 'pc_line1.png', img.size)
