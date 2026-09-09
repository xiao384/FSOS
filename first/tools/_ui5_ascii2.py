# 临时: 自适应定位 UI5 正文并降采样 ASCII
from PIL import Image
import statistics
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('L')
W,H = im.size
# GOP 3x: 逻辑(0,0) -> (32,84); 约窗口位置: 代码默认(10,16)或最大。
# 用扫描找大块白色客户区: about 窗口最大化为 320x200 减去任务栏。
# 直接对整幅做 3x 降采样(取3x3块中值)得到 ~341x256, 但实际有效区 960x600 ->320x200
ox,oy = 32,84
S=3
# 收集每逻辑像素 3x3 亮度中值
def lget(lx,ly):
    vals=[]
    for dy in range(S):
        for dx in range(S):
            vals.append(im.getpixel((ox+lx*S+dx, oy+ly*S+dy)))
    vals.sort()
    return vals[len(vals)//2]

# 打印 逻辑 y 40..120 范围 (内容区), x 0..200 (窗口足够宽)
rows=[]
for ly in range(36, 124):
    vals=[lget(lx,ly) for lx in range(0,201)]
    med=statistics.median(vals)
    lo=min(vals); hi=max(vals)
    # 若该行几乎没有变化(纯背景)跳过内容
    span=hi-lo
    if span<40:
        rows.append('.'*201)
        continue
    thr=med
    rows.append(''.join('#' if v<thr-10 else (':' if v<thr+10 else '.') for v in vals))
# 打印非空行(压缩)
prev_blank=True
for ly,r in enumerate(rows):
    if r.strip('.'):
        print('%3d %s'%(36+ly, r))
