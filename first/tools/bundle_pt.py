# ============================================================
# bundle_pt.py - 把 apps/pt 的模块化源码拼成内核可用的单文件
#
# 为什么需要"拼合":
#   内核里的 MicroPython 关闭了外部 import (MICROPY_ENABLE_EXTERNAL_IMPORT=0),
#   且 mp_entry.c 是用 mp_exec_str 执行一整段源码字符串, 所以内核侧只能是
#   一个自包含文件。而在仓库里我们又希望能像正常 Python 包那样分模块维护
#   (core/ host/ kernel/ pkg/), 于是用一个拼合步骤把二者统一:
#
#       core/ + pkg/ + kernel/  ---->  pyroot/bt.py  ---->  gen_frozen_fsos.c
#
# 拼合规则:
#   1) 按 BUNDLE_ORDER 顺序拼接, 保证"被依赖者在前";
#   2) 删掉所有跨模块 import (core./pkg./kernel./host.), 因为拼合后这些
#      名字已是同一文件的全局名; 这就是 core/ptos.py 里注释强调
#      "只能导入名字, 不能导入模块后再取属性" 的原因;
#   3) 末尾追加 run(), 作为内核入口 (main_kernel.py 自身不自动运行,
#      方便测试脚本只导入符号)。
#
# 同时负责把 packages/<name>/ 打成 .zip 并 base64 内嵌进
# pkg/frozen_pkgs.py, 作为内核里 install 命令的包来源。
#
# 用法:
#   python tools/bundle_pt.py               # 生成 frozen_pkgs.py + pyroot/bt.py
#   python tools/bundle_pt.py --frozen      # 另外调用 gen_frozen.py 更新 C 源
# ============================================================
import argparse
import base64
import io
import os
import re
import sys
import zipfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
FIRST_DIR = os.path.dirname(TOOLS_DIR)
PT_DIR = os.path.join(FIRST_DIR, 'apps', 'pt')
PKG_DIR = os.path.join(PT_DIR, 'packages')
OUT_PY = os.path.join(FIRST_DIR, 'pyroot', 'bt.py')
FROZEN_PY = os.path.join(PT_DIR, 'pkg', 'frozen_pkgs.py')
# 拼合末尾追加的覆盖段: 内核屏只能显示 ASCII, 用它替换中文帮助文本
HELP_ASCII = os.path.join(PT_DIR, 'kernel', 'help_ascii.py')

# 拼合顺序 = 依赖顺序
BUNDLE_ORDER = [
    'core/backend.py',
    'core/config.py',
    'pkg/b64.py',
    'pkg/inflate.py',
    'pkg/tinyunzip.py',
    'pkg/frozen_pkgs.py',
    'pkg/ptpkg.py',
    'kernel/fs_kernel.py',
    'kernel/backend_kernel.py',
    'core/ptos.py',
    'core/commands.py',
    'kernel/main_kernel.py',
]

# 跨模块 import: 拼合时必须剔除
INTERNAL_IMPORT = re.compile(
    r'^\s*(?:from\s+(?:core|pkg|kernel|host)\b|'
    r'from\s+\.|'
    r'import\s+(?:core|pkg|kernel|host)\b)'
)


# ============================================================
# 1. 生成内嵌包
# ============================================================
def build_frozen_pkgs():
    if not os.path.isdir(PKG_DIR):
        print('[!] 没有 packages/ 目录, 跳过内嵌包生成')
        return {}
    result = {}
    for name in sorted(os.listdir(PKG_DIR)):
        src = os.path.join(PKG_DIR, name)
        if not os.path.isdir(src):
            continue
        desc = ''
        desc_file = os.path.join(src, 'PKG.DESC')
        if os.path.isfile(desc_file):
            with open(desc_file, 'r', encoding='utf-8') as f:
                desc = f.read().strip().replace('\n', ' ')
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(src):
                # 就地排除构建产物与版本控制目录: 否则 py_compile / import
                # 留下的 __pycache__ 会被一起打进包, 既浪费空间, 又因
                # "main.cpython-313.pyc" 这类长名字直接触发内核 24 字符限制。
                dirs[:] = sorted(d for d in dirs
                                 if d not in ('__pycache__', '.git', '.idea'))
                for fn in sorted(files):
                    if fn == 'PKG.DESC' or fn.endswith('.pyc'):
                        continue
                    full = os.path.join(root, fn)
                    arc = os.path.relpath(full, src).replace('\\', '/')
                    zf.write(full, arc)
        raw = buf.getvalue()
        result[name] = (desc, base64.b64encode(raw).decode('ascii'))
        print('  [pkg] %-12s %6d 字节 -> base64 %d 字符' %
              (name, len(raw), len(result[name][1])))
    return result


def write_frozen_pkgs(pkgs):
    lines = [
        '# 由 tools/bundle_pt.py 自动生成, 请勿手工编辑',
        '# 内嵌包: 打包进内核的 .zip (base64), 供内核终端 install <包名> 使用',
        'FROZEN_PKGS = {',
    ]
    for name in sorted(pkgs):
        desc, blob = pkgs[name]
        lines.append("    '%s': ('%s'," % (name, desc.replace("'", "\\'")))
        lines.append("            '%s')," % blob)
    lines.append('}')
    lines.append('')
    with open(FROZEN_PY, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print('  -> %s' % FROZEN_PY)


# ============================================================
# 2. 拼合单文件
# ============================================================
def bundle():
    chunks = []
    chunks.append('# 由 tools/bundle_pt.py 自动生成, 请勿手工编辑。')
    chunks.append('# 源文件: apps/pt 下的 core/ pkg/ kernel/ (见 BUNDLE_ORDER)')
    chunks.append('')
    for rel in BUNDLE_ORDER:
        path = os.path.join(PT_DIR, rel.replace('/', os.sep))
        if not os.path.isfile(path):
            raise SystemExit('缺少源文件: %s' % path)
        with open(path, 'r', encoding='utf-8') as f:
            src = f.read()
        kept = []
        dropped = 0
        lines = src.split('\n')
        i = 0
        while i < len(lines):
            line = lines[i]
            if INTERNAL_IMPORT.match(line):
                dropped += 1
                # 多行 import: 形如 "from pkg.x import (a,\n  b,\n  c)"
                # 括号未闭合时, 后续续行同样属于这条 import, 必须一并剔除,
                # 否则残留的续行会变成"意外的缩进"语法错误。
                depth = line.count('(') - line.count(')')
                while depth > 0 and i + 1 < len(lines):
                    i += 1
                    dropped += 1
                    depth += lines[i].count('(') - lines[i].count(')')
                i += 1
                continue
            kept.append(line)
            i += 1
        chunks.append('# ' + '=' * 68)
        chunks.append('# from %s' % rel)
        chunks.append('# ' + '=' * 68)
        chunks.append('\n'.join(kept).rstrip())
        chunks.append('')
        print('  [+] %-28s %5d 行 (剔除 %d 行内部 import)' %
              (rel, len(kept), dropped))
    # 内核特化覆盖段: 覆盖同名常量 (目前只有 HELP_LINES)
    if os.path.isfile(HELP_ASCII):
        with open(HELP_ASCII, 'r', encoding='utf-8') as f:
            chunks.append('# ' + '=' * 68)
            chunks.append('# override from kernel/help_ascii.py (内核 ASCII 终端)')
            chunks.append('# ' + '=' * 68)
            chunks.append(f.read().rstrip())
            chunks.append('')

    chunks.append('# 内核入口: 由打包脚本追加 (main_kernel.py 自身不自动运行)')
    chunks.append('run()')
    chunks.append('')
    text = '\n'.join(chunks)
    check_mp_compat(text)                       # MP 语法兼容 gate, 失败则中止打包
    with open(OUT_PY, 'w', encoding='utf-8') as f:
        f.write(text)
    print('  -> %s (%d 字节)' % (OUT_PY, len(text)))
    return text


# ============================================================
# 3. MP 语法兼容自检 (gate)
# ============================================================
# 内嵌 MP 是 1.22 + ROM_LEVEL_MINIMUM:
#   - MICROPY_PY_FSTRINGS=0        -> f-string 一律 SyntaxError
#   - MICROPY_PY_BUILTINS_STR_UNICODE=0 -> str 字面量里 '\uXXXX'(>=0x100) 词法非法
# 这两类问题 CPython compile 检测不到, 过去曾导致内嵌 BT 每次运行都 SyntaxError,
# 终端显示 "Loading terminal..." 后永远起不来。故打包时直接拦截。
def check_mp_compat(text):
    import tokenize as _tk
    bad = []
    # 1) f-string: 逐行粗检 (f 前缀紧跟引号; bytes/r 前缀不误报)
    f_pat = re.compile(r"(?<![0-9A-Za-z_])[rRbB]*[fF](?=['\"])|"
                       r"(?<![0-9A-Za-z_])[fF][rRbB]*(?=['\"])")
    for ln, line in enumerate(text.split('\n'), 1):
        s = line.split('#', 1)[0]               # 去掉注释(含 '#' 字符串字面量会误切, 可接受)
        for m in f_pat.finditer(s):
            bad.append('L%d f-string: %r' % (ln, s.strip()[:90]))
    # 2) str 字面量内 \u/\U >=0x100: 用 CPython tokenize 拿真实字符串 token
    try:
        import io as _io
        for t in _tk.generate_tokens(_io.StringIO(text).readline):
            if t.type == _tk.STRING:
                s = t.string
                i = 0
                while i < len(s) and s[i] in 'rRbBfF':
                    i += 1
                pref, body = s[:i].lower(), s[i:]
                if 'b' in pref:
                    continue                    # bytes: \u 按原样保留, 安全
                for m in re.finditer(r'\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})', body):
                    hexs = m.group(1) or m.group(2)
                    if int(hexs, 16) >= 0x100:
                        bad.append('L%d unicode-escape: %r' % (t.start[0], s[:90]))
    except Exception as e:
        print('  [!] MP compat tokenize 阶段异常(继续): %r' % e)
    if bad:
        raise SystemExit('MP1.22(ROM_LEVEL_MINIMUM) 不兼容语法, 请改源码后重打包:\n  '
                         + '\n  '.join(bad[:12]))
    print('  [ok] MP1.22 语法兼容检查通过')


def main():
    ap = argparse.ArgumentParser(description='打包 pt 终端到内核')
    ap.add_argument('--frozen', action='store_true',
                    help='同时调用 gen_frozen.py 更新 gen_frozen_fsos.c')
    args = ap.parse_args()

    print('[1/2] 生成内嵌包')
    pkgs = build_frozen_pkgs()
    write_frozen_pkgs(pkgs)

    print('[2/2] 拼合单文件')
    text = bundle()

    # 语法自检: 拼出来的文件必须能被编译
    try:
        compile(text, OUT_PY, 'exec')
    except SyntaxError as e:
        raise SystemExit('拼合结果存在语法错误: %s' % e)
    print('  [ok] 语法检查通过')

    if args.frozen:
        gen = os.path.join(FIRST_DIR, 'pyroot', 'gen_frozen.py')
        if not os.path.isfile(gen):
            print('  [!] 未找到 %s' % gen)
            return 1
        import subprocess
        rc = subprocess.call([sys.executable, gen])
        if rc != 0:
            return rc
        print('  [ok] gen_frozen_fsos.c 已更新')
    return 0


if __name__ == '__main__':
    sys.exit(main())
