# 临时: 对比 header 中 bitmaps 与直接渲染字形是否一致
import os
import re
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
    for y in range(nh):
        for x in range(nw):
            if data[x, y] > thr:
                gx = ox + x
                gy = oy + y
                if 0 <= gx < CJK_W and 0 <= gy < CJK_W:
                    grid[gy][gx] = 1
    out = bytearray()
    for row in grid:
        v = 0
        for bit in row:
            v = (v << 1) | bit
        out += v.to_bytes(2, 'big')
    return bytes(out)


chars = []
for area in range(0xA1, 0xAA):
    for pos in range(0xA1, 0xFF):
        try:
            s = bytes([area, pos]).decode('gb2312')
            for ch in s:
                if ch != '\ufffd' and len(ch) == 1 and ord(ch) >= 0x80:
                    chars.append(ch)
        except Exception:
            pass
for area in range(0xB0, 0xD8):
    for pos in range(0xA1, 0xFF):
        try:
            s = bytes([area, pos]).decode('gb2312')
            for ch in s:
                if ch != '\ufffd' and len(ch) == 1 and ord(ch) >= 0x80:
                    chars.append(ch)
        except Exception:
            pass
seen = set()
uniq = []
for ch in chars:
    if ch not in seen:
        seen.add(ch)
        uniq.append(ch)
chars = uniq
print('chars', len(chars))
glyphs = [render(ch) for ch in chars]
glyph_index = {ord(ch): i for i, ch in enumerate(chars)}

text = open('user/cjk_font.h', 'r', encoding='utf-8').read()
cps_h = [int(x, 16) for x in re.findall(
    r'0x[0-9A-Fa-f]{4}',
    re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};',
              text, re.S)
arr = []
for line in m.group(1).splitlines():
    arr.extend(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
bitmaps = [arr[i * 12:(i + 1) * 12] for i in range(len(cps_h))]
lookup = {cp: i for i, cp in enumerate(cps_h)}
print('codepoints in header', len(cps_h))
ok = 0
for code in [0x5F00, 0x59CB, 0x8BBE, 0x7F6E, 0x684C, 0x9762]:
    p = glyph_index[code]
    q = lookup[code]
    glyph = glyphs[p]
    header = b''.join(bytes(((bitmaps[q][i] >> 8) & 0xff, bitmaps[q][i] & 0xff))
                      for i in range(12))
    match = glyph == header
    if match:
        ok += 1
    print(hex(code), 'match=', match, 'charsidx=', p, 'sortedidx=', q)
    print('  glyph :', glyph.hex())
    print('  header:', header.hex())
print('matched', ok, '/ 6')
