# 临时: 对比 出错汉字 的 header字形 vs PIL标准字形 (ASCII并排)
import re, os
from PIL import Image, ImageFont, ImageDraw

CJK_W = 12
SC = 3
font = None
for p in [r'C:\Windows\Fonts\msyhbd.ttc', r'C:\Windows\Fonts\msyh.ttc',
          r'C:\Windows\Fonts\simhei.ttf', r'C:\Windows\Fonts\simsun.ttc']:
    if os.path.exists(p):
        try:
            font = ImageFont.truetype(p, CJK_W * SC)
            break
        except Exception:
            pass
assert font, 'no font'

def render(ch):
    px = CJK_W * SC
    m = SC
    img = Image.new('L', (px + m * 2, px + m * 2), 0)
    d = ImageDraw.Draw(img)
    d.text((m, m), ch, font=font, fill=255)
    bbox = img.getbbox()
    if not bbox:
        return None
    crop = img.crop(bbox)
    bw = bbox[2] - bbox[0]
    bh = bbox[3] - bbox[1]
    scale = (CJK_W - 0.4) / max(bw, bh)
    nw = max(1, round(bw * scale))
    nh = max(1, round(bh * scale))
    small = crop.resize((nw, nh), Image.LANCZOS)
    data = small.load()
    samples = sorted(data[x, y] for y in range(nh) for x in range(nw)
                     if data[x, y] > 48)
    thr = max(100, min(210, samples[len(samples) // 2] + 26)) if samples else 128
    grid = [[0] * CJK_W for _ in range(CJK_W)]
    ox = (CJK_W - nw) // 2
    oy = (CJK_W - nh) // 2
    for yy in range(nh):
        for xx in range(nw):
            if data[xx, yy] > thr:
                gx = ox + xx
                gy = oy + yy
                if 0 <= gx < CJK_W and 0 <= gy < CJK_W:
                    grid[gy][gx] = 1
    return grid

text = open('user/cjk_font.h', 'r', encoding='utf-8').read()
cps = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
      re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
arr = []
for line in m.group(1).splitlines():
    arr.extend(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
bitmaps = [arr[i * 12:(i + 1) * 12] for i in range(len(cps))]
cp2i = {cp: i for i, cp in enumerate(cps)}

def header_grid(cp):
    i = cp2i.get(cp, -1)
    gl = bitmaps[i]
    return [[(gl[row] >> (11 - col)) & 1 for col in range(12)]
            for row in range(12)]

def fmt(g):
    return "\n".join("".join("#" if b else "." for b in row) for row in g)

for ch in "盒窗键捷设置退出门格中开始应用":
    cp = ord(ch)
    if cp < 0x80:
        continue
    hg = header_grid(cp)
    sg = render(ch)
    lines = fmt(hg).split("\n")
    slines = fmt(sg).split("\n")
    print(f"===== U+{cp:04X} '{ch}'   header | standard =====")
    for a, b in zip(lines, slines):
        print(f"  {a}  |  {b}")
    # 差异计数
    diff = sum(1 for r in range(12) for c in range(12)
               if hg[r][c] != sg[r][c])
    print(f"  diff={diff}/144")
    print()
