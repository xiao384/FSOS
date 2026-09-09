# 临时: 将 UI5 截图"关于"窗口内容区降采样为 ASCII, 与字库数据比对
from PIL import Image
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
W,H = im.size
print('ui5 size', W, H)
# GOP 整数放大 3x, 逻辑320x200, 居中偏移 ox=(W-960)/2, oy=(H-600)/2
ox,oy = (W-320*3)//2, (H-200*3)//2
print('offset', ox, oy)

# 找 "关于" 窗口: 扫描白/亮底窗口 (about 客户区 COL_WHITE)
# 内容区文字行间隔 16 逻辑=48px; 文本从 (12,32)逻辑开始 5 行
# 逐行打印逻辑 y 区域
def ascii_line(y):
    row=[]
    for x in range(320):
        v=im.getpixel((ox+x*3+1, oy+y*3+1))
        row.append('#' if v<128 else (':' if v<190 else '.'))
    return ''.join(row)

# about 窗口标题栏 ~ 逻辑 y 16..31, 内容 5 行: y+6,22,38,54,70 (相对内容y0=32)
# 标题 "关于" 在 逻辑(16,18)
print('== 标题栏行 (逻辑y 18..29) ==')
for y in range(18,30):
    print(ascii_line(y))
print('== 内容行区域 (逻辑y 32..115), 每 16 行采样 ==')
for y in list(range(34, 116, 16)):
    print('--- y', y)
    for yy in range(y, y+12):
        print(ascii_line(yy))
