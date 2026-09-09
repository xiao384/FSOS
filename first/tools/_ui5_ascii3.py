# 临时: 精确定位+ASCII 化 UI5 "关于"正文区
from PIL import Image
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
W,H=im.size
# 整幅网格图(8px采样)显示: 客户区 x约72..608, y约184..488 为'+' (60-140亮度)
# 文字在背景上更暗。正文5行大致 y=210,258,306,354,402 (48px=16逻辑x3间隔)
# 直接对每候选行扫描: 用行内中值做阈值, 输出每 3px(逻辑1px)一列
import statistics
def line_ascii(y0, x0=70, x1=620):
    # 采集该 12 逻辑高(36px)带内各像素; 以背景(中值偏高)为阈值
    out=[]
    for lx in range(0, (x1-x0)//3):
        vals=[]
        for dy in range(0,36,2):
            for dx in range(0,3):
                vals.append(im.getpixel((x0+lx*3+dx, y0+dy)))
        vals.sort()
        bg=vals[int(len(vals)*0.75)]   # 高值=背景
        fg=vals[len(vals)//8]
        if bg-fg<15:
            out.append('?'); continue
        mid=(bg+fg)//2
        v=vals[len(vals)//2]
        out.append('#' if v<=mid else '.')
    return ''.join(out)

for name,y0 in [('L1',214),('L2',262),('L3',310),('L4',358),('L5',406),('TITLE',152)]:
    s=line_ascii(y0)
    # 只显示 x0~x0+ (每字符=3*12逻辑? no, 此输出已经按逻辑lx, x每字12列)
    print(name, s[:150])
