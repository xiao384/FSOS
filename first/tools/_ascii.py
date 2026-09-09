# 临时: 将 PC 渲染(与内核完全同算法)输出为文本像素图, 客观判断粘连
from PIL import Image
import re

im = Image.open(r'output/pc_line1.png').convert('L')
W, H = im.size  # 800x360 (4x 放大)
px = im.load()
# 缩回逻辑 200x90
sx, sy = W // 200, H // 90
print('== PC 渲染 (当前 cjk_font.h, 与内核同算法), 逻辑分辨率像素 ==')
for y in range(0, 90):
    row = ''
    for x in range(0, 200):
        v = px[x * sx, y * sy]
        row += '#' if v < 128 else '.'
    print(row)
