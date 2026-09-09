# 临时: 检查 wm.c / desktop.c 内中文字符串编码 (是否 UTF-8 vs GBK)
import re

def analyze(path, targets):
    raw = open(path, 'rb').read()
    print(f"=== {path}  size={len(raw)} ===")
    print("BOM:", raw[:3].hex())
    try:
        raw.decode('utf-8')
        print("whole file: valid UTF-8")
    except Exception as e:
        print("whole file: NOT utf-8 ->", e)
    # 找所有连续非 ASCII 字节段, 尝试解码
    segs = re.findall(rb'[\x80-\xff]{2,}', raw)
    utf8ok = 0; gbkok = 0
    samples = []
    for seg in segs:
        try:
            seg.decode('utf-8'); utf8ok += 1
        except Exception:
            pass
        try:
            seg.decode('gbk'); gbkok += 1
        except Exception:
            pass
    print(f"non-ascii segments={len(segs)} utf8ok={utf8ok} gbkok={gbkok}")
    # 关键: 取前几个长段打印 16 进制 + 两种解码
    longs = [s for s in segs if len(s) >= 4][:6]
    for seg in longs:
        print("seg hex:", seg.hex())
        print("  utf8:", repr(seg.decode('utf-8', 'replace')))
        print("  gbk :", repr(seg.decode('gbk', 'replace')))
    # targets 用 unicode 转义指定
    for label, u in targets.items():
        b = u.encode('utf-8')
        idx = raw.find(b)
        print(f"target {label} U+{u} utf8-bytes={b.hex()} found@{idx}")

analyze(r'E:\project\clion\project_system\first\user\wm.c', {
    'pixel': '\u50cf\u7d20\u6c99\u76d2',   # 自由安全操作系统
    'about': '\u5173\u4e8e',                # 关于
    'style': '\u98ce\u683c\u4e2d\u6587\u684c\u9762',  # 风格中文桌面
    'set':   '\u8bbe\u7f6e',                # 设置
    'win':   '\u7a97\u53e3',                # 窗口
})
analyze(r'E:\project\clion\project_system\first\user\desktop.c', {
    'about': '\u5173\u4e8e',
    'set':   '\u8bbe\u7f6e',
})
