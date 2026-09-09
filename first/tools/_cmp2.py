# 临时: header字形 vs PIL标准字形 ASCII 对比 -> utf8 文件
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
    bw = bbox[2] - bbox[0]; bh = bbox[3] - bbox[1]
    scale = (CJK_W - 0.4) / max(bw, bh)
    nw = max(1, round(bw * scale)); nh = max(1, round(bh * scale))
    small = crop.resize((nw, nh), Image.LANCZOS)
    data = small.load()
    samples = sorted(data[x, y] for y in range(nh) for x in range(nw) if data[x, y] > 48)
    thr = max(100, min(210, samples[len(samples)//2] + 26)) if samples else 128
    grid = [[0]*CJK_W for _ in range(CJK_W)]
    ox = (CJK_W - nw)//2; oy = (CJK_W - nh)//2
    for yy in range(nh):
        for xx in range(nw):
            if data[xx, yy] > thr:
                gx = ox + xx; gy = oy + yy
                if 0 <= gx < CJK_W and 0 <= gy < CJK_W: grid[gy][gx] = 1
    return grid

text = open(r'E:\project\clion\project_system\first\user\cjk_font.h', 'r', encoding='utf-8').read()
cps = [int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
      re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
arr = []
for line in m.group(1).splitlines():
    arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
# 断言数量
n_expected = 4437
print("total uint16 parsed:", len(arr), "expect", n_expected*12, file=open('_glyph_diff.txt','w'))
bitmaps = [arr[i*12:(i+1)*12] for i in range(n_expected)]
cp2i = {cp:i for i,cp in enumerate(cps)}
print("cps count:", len(cps), "unique:", len(cp2i), file=open('_glyph_diff.txt','a'))

def header_grid(cp):
    i = cp2i.get(cp)
    if i is None: return None
    gl = bitmaps[i]
    return [[(gl[row]>>(11-col))&1 for col in range(12)] for row in range(12)]

def fmt(g):
    return "\n".join("".join("#" if b else "." for b in row) for row in g)

lines = []
for ch in "\u5173\u4e8e\u50cf\u7d20\u6c99\u76d2\u98ce\u683c\u4e2d\u6587\u684c\u9762\u53e3\u952e\u7f6e\u8bbe":
    cp = ord(ch)
    hg = header_grid(cp)
    sg = render(ch)
    lines.append(f"===== U+{cp:04X} '{ch}'  (header glyph vs PIL standard) =====")
    if hg is None:
        lines.append("  MISSING in header!")
    else:
        for a,b in zip(fmt(hg).split("\n"), fmt(sg).split("\n")):
            lines.append(f"  {a}  |  {b}")
        diff = sum(1 for r in range(12) for c in range(12) if hg[r][c]!=sg[r][c])
        lines.append(f"  diff={diff}/144")
    lines.append("")
open('_glyph_diff.txt','a',encoding='utf-8').write("\n".join(lines))
print("done")
