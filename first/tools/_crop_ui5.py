# 临时: 裁剪 UI5 截图"关于"窗口正文区, 放大观察重叠 vs 密集
from PIL import Image
im = Image.open(r'..\output\UI_Chinese\ui5_v1.png').convert('RGB')
print('orig', im.size)
# 逻辑 320x200 放大到 960x600 (scale 3)。窗口(10,16,176x116) -> (30,48,528,348)
# 截取正文区: 逻辑 x8..x180, y20..y135 -> 3x
crop = im.crop((24, 60, 545, 410))
crop = crop.resize((crop.width * 2, crop.height * 2), Image.NEAREST)
crop.save(r'output\ui5_body_zoom.png')
print('saved', crop.size)
