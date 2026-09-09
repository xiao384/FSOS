# gen_cjk_font.py - 生成 CJK 12x12 点阵字库 header (user/cjk_font.h)
#
# 用途: FSOS 桌面环境 (320x200, 8bpp) 显示中文 UI。英文字符沿用引导器装入
#       0xB0000 的 8x8 VGA 字体; 汉字/全角符号由本脚本离线渲染成 12x12 点阵,
#       编译期内嵌进内核 (运行时无文件系统依赖)。
#
# 覆盖范围 (约 4400 字, ~105KB, 一劳永逸免维护):
#   - GB2312 一级汉字 (3755 个, 拼音序, 覆盖绝大多数字界面用字)
#   - GB2312 符号区 (A1A1..A9FE 中所有可解码字符: 、。〈〉《》… 等)
#   若出现缺字, 运行本脚本追加渲染即可 (默认全量, 无需维护清单)。
#
# 渲染: simhei (黑体, 无衬线, 适合小字号屏幕) 以 36px 超采样渲染,
#       字形按包围盒居中后 LANCZOS 缩到 12x12, 中位灰度二值化。
# 布局: 每字 12 行, 每行 12 bit 存于 uint16_t 低 12 位 (高位补 0)。
#       字形笔画=1, 空白=0; 绘制函数按 12x12 逐行描像素。
#
# 输出: user/cjk_font.h —— 两个静态数组 + 二分查找辅助, 供 cjk.c include。
# 用法: python tools/gen_cjk_font.py [--out path] [--preview "汉字串"]
# 依赖: Pillow + Windows 字体 (msyh/msyhbd/simhei/simsun), 构建机为 Windows。
import argparse
import sys
import os

CJK_W = 12
CJK_H = 12
ROWS_BYTES = 2          # 每行 12bit 存 uint16_t 低 12 位
SC = 3                  # 超采样倍率

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyhbd.ttc",   # 微软雅黑 Bold
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\simsun.ttc",
]

def pick_font(px):
    from PIL import ImageFont
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            try:
                return p, ImageFont.truetype(p, px)
            except Exception:
                continue
    # 无 Windows 字体兜底: 从 matplotlib 找不到则报错
    raise SystemExit("no CJK font found; need msyh/simhei/simsun in C:\\Windows\\Fonts")

def gb2312_chars():
    """枚举 GB2312 一级汉字 (B0A1..D7F9) + 符号区 (A1A1..A9FE)。"""
    chars = []
    # 符号区 A1..A9 (含全角标点/序号/单位等常用字符)
    for area in range(0xA1, 0xAA):
        for pos in range(0xA1, 0xFF):
            try:
                b = bytes([area, pos])
                s = b.decode("gb2312")
            except Exception:
                continue
            for ch in s:
                if ch != "\ufffd":
                    chars.append(ch)
    # 一级汉字 B0..D7
    for area in range(0xB0, 0xD8):
        for pos in range(0xA1, 0xFF):
            try:
                b = bytes([area, pos])
                s = b.decode("gb2312")
            except Exception:
                continue
            for ch in s:
                if ch != "\ufffd":
                    chars.append(ch)
    # 去重保序 (gb2312 两个字节区互不重叠, 通常已有序)
    seen = set()
    out = []
    for ch in chars:
        if ch not in seen and len(ch) == 1 and ord(ch) >= 0x80:
            seen.add(ch)
            out.append(ch)
    return out

def render_glyph(font, ch, size):
    """渲染单字 -> 12x12 0/1 行列表 (每行 12 bit)。

    流程: 在 SC 倍尺寸的画布上用大字号渲染(抗锯齿) -> 按字形包围盒裁剪 ->
    等比例缩到约 size-1 像素并置入 size 网格 -> 中位灰度阈值二值化。
    """
    from PIL import Image, ImageDraw
    px = size * SC          # 超采样画布边长
    m = SC                  # 画布留边
    img = Image.new("L", (px + m * 2, px + m * 2), 0)
    d = ImageDraw.Draw(img)
    d.text((m, m), ch, font=font, fill=255)
    bbox = img.getbbox()
    if bbox is None:
        return [[0] * size for _ in range(size)]
    bw = bbox[2] - bbox[0]
    bh = bbox[3] - bbox[1]
    if bw <= 0 or bh <= 0:
        return [[0] * size for _ in range(size)]
    crop = img.crop((bbox[0], bbox[1], bbox[2], bbox[3]))
    scale = (size - 0.4) / float(max(bw, bh))   # 留少量像素级空隙, 避免贴边
    nw = max(1, round(bw * scale))
    nh = max(1, round(bh * scale))
    small = crop.resize((nw, nh), 3) if (nw, nh) != crop.size else crop
    # 反走样灰度 -> 阈值 (取笔画灰度的中位偏亮, 保证小字号不糊)
    data = small.load()
    samples = sorted(data[x, y] for y in range(nh) for x in range(nw)
                     if data[x, y] > 48)
    thr = 128
    if samples:
        thr = max(100, min(210, samples[len(samples) // 2] + 26))
    grid = [[0] * size for _ in range(size)]
    ox = (size - nw) // 2
    oy = (size - nh) // 2
    for y in range(nh):
        for x in range(nw):
            if data[x, y] > thr:
                gx = ox + x
                gy = oy + y
                if 0 <= gx < size and 0 <= gy < size:
                    grid[gy][gx] = 1
    return grid

def glyph_bytes(grid):
    out = bytearray()
    for row in grid:
        v = 0
        for bit in row:
            v = (v << 1) | bit
        out += v.to_bytes(2, "big")   # 低 12 位, 高位清 0
    return bytes(out)

def ascii_preview(grid):
    return "\n".join("".join("#" if b else "." for b in row) for row in grid)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None)
    ap.add_argument("--preview", default="")
    ap.add_argument("--preview-file", default="")
    ap.add_argument("--chars", default="")
    args = ap.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_path = args.out or os.path.normpath(os.path.join(
        script_dir, "..", "user", "cjk_font.h"))
    font_path, font = pick_font(CJK_W * SC)

    chars = gb2312_chars()
    if args.chars:
        extra = []
        for ch in args.chars:
            cp = ord(ch)
            if cp < 0x80:
                continue
            if ch not in chars:
                extra.append(ch)
        for ch in extra:
            chars.append(ch)

    # 渲染所有字形
    glyphs = []
    for i, ch in enumerate(chars):
        g = render_glyph(font, ch, CJK_W)
        glyphs.append(glyph_bytes(g))
        if i % 500 == 0:
            print(f"  rendering {i}/{len(chars)} ...", flush=True)

    # 预览 (供在无图形环境人工核验字形; 输出全 ASCII, 规避 GBK 控制台问题)
    preview_text = ""
    if args.preview_file:
        with open(args.preview_file, "r", encoding="utf-8") as f:
            preview_text = f.read()
    if args.preview:
        preview_text = args.preview
    if preview_text:
        print("\n==== glyph preview ====")
        for ch in preview_text:
            cp = ord(ch)
            if cp < 0x80:
                continue
            try:
                idx = chars.index(ch)
            except ValueError:
                print(f"[U+{cp:04X}] NOT in font set")
                continue
            g = glyphs[idx]
            grid = []
            for y in range(CJK_H):
                v = int.from_bytes(g[y * 2:y * 2 + 2], "big")
                grid.append([(v >> (11 - x)) & 1 for x in range(CJK_W)])
            print(f"--- U+{cp:04X} ---")
            print(ascii_preview(grid))
        return 0


    # 组装 C header
    cps = sorted(ord(ch) for ch in chars)      # 码点升序, 便于二分
    # 关键: glyphs 数组按 chars(GB2312/原始)顺序存放, 必须按码点顺序取出,
    #       而不能用 chars 循环时的 sorted-index 直接取。
    glyph_index = {ord(ch): i for i, ch in enumerate(chars)}
    data = bytearray()
    for cp in cps:
        data += glyphs[glyph_index[cp]]

    lines = []
    lines.append("// cjk_font.h - CJK 12x12 点阵字库 (自动生成, 勿手改)")
    lines.append(f"// 由 tools/gen_cjk_font.py 生成: {len(chars)} chars, "
                     f"{len(data) // 1024} KB, font={os.path.basename(font_path)}")
    lines.append("#ifndef CJK_FONT_HEADER")
    lines.append("#define CJK_FONT_HEADER")
    lines.append("#include <stdint.h>")
    lines.append(f"#define CJK_FONT_W {CJK_W}")
    lines.append(f"#define CJK_FONT_H {CJK_H}")
    lines.append(f"#define CJK_FONT_N {len(chars)}")
    lines.append("// 码点升序表 (用于二分查找)")
    lines.append("static const uint32_t cjk_codepoints[CJK_FONT_N] = {")

    row = []
    for cp in cps:
        row.append("0x%04X" % cp)
        if len(row) == 12:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    lines.append("// 字形数据: 每字 12 行 x uint16_t (低 12 位), 笔画=1")
    lines.append("static const uint16_t cjk_bitmaps[CJK_FONT_N][CJK_FONT_H] = {")
    per = 4  # 每行放 4 个字
    cnt = 0
    buf = []
    # 注意: 必须与 cjk_codepoints 同序(cps 码点升序), 即遍历排序后的 data
    GSZ = CJK_H * 2
    for off in range(0, len(data), GSZ):
        g = data[off:off + GSZ]
        arr = ", ".join("0x%04X" % int.from_bytes(g[y * 2:y * 2 + 2], "big")
                        for y in range(CJK_H))
        buf.append("{ " + arr + " }")
        cnt += 1
        if cnt % per == 0:
            lines.append("    " + ", ".join(buf) + ",")
            buf = []
    if buf:
        lines.append("    " + ", ".join(buf) + ",")
    lines.append("};")
    lines.append("// 查表: 返回字形指针或 NULL")
    lines.append("static inline const uint16_t* cjk_lookup(uint32_t cp) {")
    lines.append("    int lo = 0, hi = CJK_FONT_N - 1;")
    lines.append("    while (lo <= hi) {")
    lines.append("        int mid = (lo + hi) >> 1;")
    lines.append("        if (cjk_codepoints[mid] == cp) return cjk_bitmaps[mid];")
    lines.append("        if (cjk_codepoints[mid] < cp) lo = mid + 1; else hi = mid - 1;")
    lines.append("    }")
    lines.append("    return 0;")
    lines.append("}")
    lines.append("#endif // CJK_FONT_HEADER")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"[OK] {len(chars)} chars -> {out_path} "
          f"({os.path.getsize(out_path) // 1024} KB header)")

if __name__ == "__main__":
    main()
