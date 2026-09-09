# 临时: 用 cjk_font.h 渲染整行, 每个字符独立输出 ASCII 点阵 (无步进重叠, 逐字符栅格)
import re

def load():
    text = open(r'E:\project\clion\project_system\first\user\cjk_font.h', 'r', encoding='utf-8').read()
    cps = [int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}',
          re.search(r'cjk_codepoints\[CJK_FONT_N\] = \{([^}]+)\}', text).group(1))]
    m = re.search(r'cjk_bitmaps\[CJK_FONT_N\]\[CJK_FONT_H\] = \{([^;]+)\};', text, re.S)
    arr = []
    for line in m.group(1).splitlines():
        arr.extend(int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]{4}', line))
    bm = [arr[i*12:(i+1)*12] for i in range(len(cps))]
    return cps, bm

cps, bm = load()
L = {cp:i for i,cp in enumerate(cps)}

def chart(cp):
    i = L.get(cp, -1)
    if i < 0:
        return f"[MISS {cp:04X}]"
    gl = bm[i]
    return "\n".join("".join("#" if (gl[r]>>(11-c))&1 else "." for c in range(12)) for r in range(12))

# 逐字符渲染目标行
for ch in "\u50cf\u7d20\u6c99\u76d2\u98ce\u683c\u4e2d\u6587\u684c\u9762\u53cc\u51fb\u56fe\u6807\u5b57\u76d8\u4e2d\u6587\u52a0\u5165\u8f93\u6cd5\u8bbf":
    cp = ord(ch)
    print(f"=== U+{cp:04X} {ch} ===")
    print(chart(cp))
    print()
