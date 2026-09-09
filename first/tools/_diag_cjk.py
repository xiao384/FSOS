# 临时诊断: 打印目标字符串每个字的码点/header命中/实际字形ASCII预览
import re, sys

text = open('user/cjk_font.h', 'r', encoding='utf-8').read()
cps = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
      re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
arr = []
for line in m.group(1).splitlines():
    arr.extend(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
bitmaps = [arr[i * 12:(i + 1) * 12] for i in range(len(cps))]
cp2i = {cp: i for i, cp in enumerate(cps)}

def prev(cp):
    i = cp2i.get(cp, -1)
    if i < 0:
        return f"[U+{cp:04X}] MISSING in header"
    gl = bitmaps[i]
    rows = []
    for row in range(12):
        bits = gl[row]
        line = ''.join('#' if (bits >> (11 - col)) & 1 else '.' for col in range(12))
        rows.append(line)
    return f"[U+{cp:04X}] idx={i}\n    " + "\n    ".join(rows)

target = "自由安全操作系统 FSOS 双击图标打开窗口 快捷键 设置 桌面 帮助 退出"
# 全角转半角 (这里全是汉字/空格, 直接按字符)
for ch in target:
    cp = ord(ch)
    if cp < 0x80:
        continue
    print(prev(cp))
    print()
