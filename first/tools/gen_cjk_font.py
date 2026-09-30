#!/usr/bin/env python3
"""Generate the compact CJK bitmap header used by the FSOS bare-metal GUI.

The previous generator stub was insufficiently deterministic and the checked-in
bitmaps contained malformed glyphs. This generator renders each selected Unicode
codepoint from a real CJK font (Noto Sans CJK SC by default), then downsamples it
with supersampling into clean 16x16 and 24x24 binary bitmaps.
"""
import argparse
from pathlib import Path
import re

from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    (r"C:\\Windows\\Fonts\\NotoSansCJK-Regular.ttc", 2),
    (r"C:\\Windows\\Fonts\\msyh.ttc", 0),
    (r"C:\\Windows\\Fonts\\simhei.ttf", 0),
    ("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 2),
]


def parse_codepoints(header_path: Path):
    text = header_path.read_text(encoding="utf-8")
    m = re.search(r"cjk_codepoints\[CJK_FONT_N\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"cannot find codepoint table in {header_path}")
    return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))]


def render_bitmap(font, ch: str, out_size: int, supersample: int = 4):
    big = out_size * supersample
    img = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), ch, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    # Keep the glyph centered with a small safety margin. The font's baseline
    # varies by codepoint, so place by bbox rather than hard-coded y offsets.
    x = (big - w) // 2 - bbox[0]
    y = (big - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=255)
    small = img.resize((out_size, out_size), Image.Resampling.LANCZOS)
    pix = small.load()
    rows = []
    for y in range(out_size):
        bits = 0
        for x in range(out_size):
            if pix[x, y] >= 96:
                bits |= 1 << (out_size - 1 - x)
        rows.append(bits)
    return rows


def fmt_u16(rows):
    return "{" + ", ".join(f"0x{v:04X}" for v in rows) + "}"


def fmt_u32(rows):
    return "{" + ", ".join(f"0x{v:06X}" for v in rows) + "}"


def generate(old_header: Path, out_header: Path, font_path: str, ttc_index: int):
    cps = parse_codepoints(old_header)
    font16 = ImageFont.truetype(font_path, 16 * 4, index=ttc_index)
    font24 = ImageFont.truetype(font_path, 24 * 4, index=ttc_index)

    bit16 = []
    bit24 = []
    for cp in cps:
        ch = chr(cp)
        bit16.append(render_bitmap(font16, ch, 16))
        bit24.append(render_bitmap(font24, ch, 24))

    lines = [
        "// cjk_font.h - CJK bitmap font (generated; do not hand edit)",
        f"// Source font: {font_path} (ttc index {ttc_index})",
        f"// {len(cps)} Unicode codepoints, 16x16 and 24x24 monochrome glyphs.",
        "#ifndef CJK_FONT_HEADER",
        "#define CJK_FONT_HEADER",
        "#include <stdint.h>",
        f"#define CJK_FONT_N {len(cps)}",
        "",
        "static const uint32_t cjk_codepoints[CJK_FONT_N] = {",
    ]
    for i in range(0, len(cps), 12):
        chunk = cps[i:i+12]
        lines.append("    " + ", ".join(f"0x{cp:04X}" for cp in chunk) + ",")
    lines += [
        "};",
        "",
        "// 16x16 monochrome bitmap, MSB is the left-most pixel.",
        "static const uint16_t cjk_bitmaps16[CJK_FONT_N][16] = {",
    ]
    for rows in bit16:
        lines.append("    " + fmt_u16(rows) + ",")
    lines += [
        "};",
        "",
        "// 24x24 monochrome bitmap, MSB is the left-most pixel.",
        "static const uint32_t cjk_bitmaps24[CJK_FONT_N][24] = {",
    ]
    for rows in bit24:
        lines.append("    " + fmt_u32(rows) + ",")
    lines += [
        "};",
        "",
        "static inline int cjk_font_find(uint32_t cp) {",
        "    int lo = 0, hi = CJK_FONT_N - 1;",
        "    while (lo <= hi) {",
        "        int mid = lo + ((hi - lo) >> 1);",
        "        uint32_t v = cjk_codepoints[mid];",
        "        if (v == cp) return mid;",
        "        if (v < cp) lo = mid + 1; else hi = mid - 1;",
        "    }",
        "    return -1;",
        "}",
        "",
        "static inline const uint16_t* cjk_lookup(uint32_t cp) {",
        "    int i = cjk_font_find(cp);",
        "    return i >= 0 ? cjk_bitmaps16[i] : (const uint16_t*)0;",
        "}",
        "",
        "static inline const uint32_t* cjk_lookup24(uint32_t cp) {",
        "    int i = cjk_font_find(cp);",
        "    return i >= 0 ? cjk_bitmaps24[i] : (const uint32_t*)0;",
        "}",
        "",
        "#endif // CJK_FONT_HEADER",
        "",
    ]
    out_header.write_text("\n".join(lines), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    default_font, default_index = next(((p, i) for p, i in FONT_CANDIDATES if Path(p).exists()), (FONT_CANDIDATES[0][0], FONT_CANDIDATES[0][1]))
    ap.add_argument("--font", default=default_font)
    ap.add_argument("--ttc-index", type=int, default=default_index)
    ap.add_argument("--source-header", default="user/cjk_font.h")
    ap.add_argument("--output", default="user/cjk_font.h")
    args = ap.parse_args()
    source = Path(args.source_header)
    output = Path(args.output)
    generate(source, output, args.font, args.ttc_index)
    print(f"generated {output} with {len(parse_codepoints(output))} glyphs")


if __name__ == "__main__":
    main()
