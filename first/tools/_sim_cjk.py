# 临时: 用当前 cjk_font.h 与 cjk.c 相同算法在 PC 渲染中文 UI 文字 (排查乱码)
import re
from PIL import Image

text = open('user/cjk_font.h', 'r', encoding='utf-8').read()
cps = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
      re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
arr = []
for line in m.group(1).splitlines():
    arr.extend(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
bitmaps = [arr[i * 12:(i + 1) * 12] for i in range(len(cps))]
lookup = {cp: i for i, cp in enumerate(cps)}

font8 = open('boot/uefi/font8x8.h', 'r', encoding='utf-8').read()
idx = font8.index('g_font8x8')
tail = font8[idx:]
g8 = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{2}', tail)][:96 * 8]
g8 = [g8[i * 8:(i + 1) * 8] for i in range(96)]


def draw_ascii(x, y, ch, fg, bg, px):
    if ch < 0x20:
        ch = '?'
    if ch >= 128:
        ch = '?'
    gl = g8[ch - 0x20]
    # font_petme128_8x8 数据是列优先: 每字节表示一列, bit0=最上
    for col in range(8):
        bits = gl[col]
        for row in range(8):
            px[x + col, y + row] = fg if (bits >> row) & 1 else bg


def draw_cjk(x, y, cp, fg, bg, px):
    for dy in range(12):
        for dx in range(12):
            px[x + dx, y + dy] = bg
    idx = lookup.get(cp, -1)
    if idx < 0:
        return
    gl = bitmaps[idx]
    for row in range(12):
        bits = gl[row]
        for col in range(12):
            if (bits >> (11 - col)) & 1:
                px[x + col, y + row] = fg


def draw_text(x, y, s, fg, bg, px):
    for ch in s:
        cp = ord(ch)
        if cp < 0x80:
            draw_ascii(x, y + 2, cp, fg, bg, px)
            x += 8
        else:
            draw_cjk(x, y, cp, fg, bg, px)
            x += 12


S = ('自由安全操作系统 FSOS\n'
     'Windows 风格中文桌面\n'
     '双击图标打开窗口\n'
     '左下角开始菜单启动应用\n'
     'ESC 退出桌面  F1 快捷键')
img = Image.new('RGB', (200, 90), (0xd4, 0xd4, 0xd4))
px = img.load()
y = 4
for ln in S.split('\n'):
    draw_text(4, y, ln, (0x55, 0x30, 0x10), (0xd4, 0xd4, 0xd4), px)
    y += 17
img.resize((img.width * 4, img.height * 4), Image.NEAREST).save('output/cjk_sim.png')
print('saved output/cjk_sim.png')
