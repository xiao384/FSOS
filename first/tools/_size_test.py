# 临时: 12x12(现内核) vs 16x16 对比渲染"自由安全操作系统FSOS", 判断升级分辨率价值
from PIL import Image, ImageDraw, ImageFont
import re

FONT = r"C:\Windows\Fonts\msyhbd.ttc"
def render_with(fontpx, grid):
    # 返回 dict cp->grid 的 0/1
    font = ImageFont.truetype(FONT, fontpx)
    chars = '自由安全操作系统风格中文桌面双击图标快捷退出应用'
    out = {}
    for ch in chars:
        px = grid * 3
        img = Image.new('L', (px + 6, px + 6), 0)
        d = ImageDraw.Draw(img)
        d.text((3, 3), ch, font=font, fill=255)
        bb = img.getbbox()
        if bb is None:
            out[ch] = [[0] * grid for _ in range(grid)]
            continue
        crop = img.crop(bb)
        bw, bh = crop.size
        scale = (grid - 0.4) / float(max(bw, bh))
        nw = max(1, round(bw * scale)); nh = max(1, round(bh * scale))
        small = crop.resize((nw, nh), 3)
        data = small.load()
        samples = sorted(data[x, y] for y in range(nh) for x in range(nw) if data[x, y] > 48)
        thr = 128
        if samples:
            thr = max(100, min(210, samples[len(samples)//2] + 26))
        g = [[0] * grid for _ in range(grid)]
        ox = (grid - nw) // 2; oy = (grid - nh) // 2
        for y in range(nh):
            for x in range(nw):
                if data[x, y] > thr:
                    gx, gy = ox + x, oy + y
                    if 0 <= gx < grid and 0 <= gy < grid:
                        g[gy][gx] = 1
        out[ch] = g
    return out

def blit(canvas, d, x, y, g, grid, scale, on=(0,0,0)):
    for r in range(grid):
        for c in range(grid):
            if g[r][c]:
                d.rectangle([(x+c)*scale,(y+r)*scale,(x+c+1)*scale-1,(y+r+1)*scale-1], fill=on)

# --- 渲染 ---
g12 = render_with(36, 12)
g16 = render_with(36, 16)
chars = '自由安全操作系统FSOS'
SC = 4
W = 6 * 16 * SC
H = (12 + 16) * SC + 40
img = Image.new('RGB', (W, H), (255,255,255))
d = ImageDraw.Draw(img)
x = 2
for ch in '自由安全操作系统':
    blit(img, d, x, 2, g12[ch], 12, SC); blit(img, d, x, 2 + 12 + 6, g16[ch], 16, SC)
    d.text((x*SC+ (12*SC), 16), '', fill=(0,0,0))
    x += 13
x = 12 + 2*13  # 跳过
# 重新布局: 每字纵向 上12 下16
img2 = Image.new('RGB', (len('自由安全操作系统') * 14 * SC, (12+2+16)*SC + 20), (255,255,255))
d2 = ImageDraw.Draw(img2)
for i, ch in enumerate('自由安全操作系统'):
    blit(img2, d2, i*14, 2, g12[ch], 12, SC)
    blit(img2, d2, i*14, 2+12+2, g16[ch], 16, SC)
img2.save(r'output\size_test.png')
print('saved', img2.size)
