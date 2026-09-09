# 临时: 逐字渲染 cjk.c 同算法的单字, 确认每个字形本身是否正确 (分离渲染)
import re
from PIL import Image, ImageDraw

text = open('user/cjk_font.h', 'r', encoding='utf-8').read()
cps = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
      re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
arr = []
for line in m.group(1).splitlines():
    arr.extend(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
bitmaps = [arr[i * 12:(i + 1) * 12] for i in range(len(cps))]
lookup = {cp: i for i, cp in enumerate(cps)}

# 用 CJK_C 的约定: gl[row], bit(11-col) = 第0列(左)
def render(cp, scale=8):
    # 在 16x16 画布(含2px留白)渲染单个 12x12 字形
    cell = 12 + 4
    img = Image.new('RGB', (cell * scale, cell * scale), (0xd4, 0xd4, 0xd4))
    dr = ImageDraw.Draw(img)
    idx = lookup.get(cp, -1)
    if idx < 0:
        dr.text((4, 4), 'MISS', fill=(255, 0, 0))
        return img
    gl = bitmaps[idx]
    for row in range(12):
        bits = gl[row]
        for col in range(12):
            if (bits >> (11 - col)) & 1:
                x = (col + 2) * scale
                y = (row + 2) * scale
                dr.rectangle([x, y, x + scale - 1, y + scale - 1], fill=(0x30, 0x20, 0x10))
    # 画 12x12 边界参考
    dr.rectangle([2 * scale, 2 * scale, (12 + 2) * scale - 1, (12 + 2) * scale - 1], outline=(200, 120, 120))
    return img

chars = '自由安全操作系统风格中文桌面双击图标打开窗口左下角菜单启动应用'
# 每行 8 个字
cols = 8
per = 16 * 8  # 每格 16*scale, scale=8 -> 128*... 太大了, 降低
scale = 4
cellpx = 16 * scale
rows = (len(chars) + cols - 1) // cols
W = cols * cellpx
H = rows * (cellpx + 20)
out = Image.new('RGB', (W, H), (0xdd, 0xdd, 0xdd))
dr = ImageDraw.Draw(out)
for i, ch in enumerate(chars):
    img = render(ord(ch), scale)
    cx = (i % cols) * cellpx
    cy = (i // cols) * (cellpx + 20)
    out.paste(img, (cx, cy))
    dr.text((cx + 4, cy + cellpx + 2), '%02X %s' % (ord(ch), ch), fill=(0, 0, 0))
out.save('output/cjk_each.png')
print('saved -> ' + repr(cps[:3]))
print('char count', len(chars))
