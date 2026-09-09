# ============================================================
# sync_pt.py - 把外部 Better terminal (pt) 项目同步为本仓库内的副本
#
# 设计原则:
#   1) 对原项目目录 (默认 D:\better terminal_project\pt) 只做读取, 绝不写入。
#      本脚本在写入任何文件前都会校验目标路径位于 --dest 之内。
#   2) 副本落在 first/apps/pt/_upstream/, 是原项目的逐字节镜像, 只用于
#      对照 diff 与重新同步; 本仓库真正使用与改造的代码在
#      core/ host/ kernel/ pkg/ 四个目录里。
#   3) 默认 dry-run: 只打印将要执行的操作, 确认无误后加 --apply 才真正复制。
#
# 用法:
#   python tools/sync_pt.py                      # 预览
#   python tools/sync_pt.py --apply              # 执行同步
#   python tools/sync_pt.py --verify             # 校验副本与原项目是否一致
# ============================================================
import argparse
import hashlib
import os
import shutil
import sys

DEFAULT_SRC = r'D:\better terminal_project\pt'

# 需要同步的源文件 (相对 src 根目录)
SYNC_FILES = [
    'main.py',
    'commands.py',
    'config.py',
    'system_os.py',
    'theme.py',
    'ui.py',
    'users_data.json',
    'Better terminal_project.py',
    # 注: Better terminal_dark.py 是旧版独立文件, 系统终端不需要它, 故不同步。
    # 如需保留对照, 请自行在源目录保留 (本脚本不修改源目录)。
]

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DEST = os.path.join(REPO_ROOT, 'apps', 'pt', '_upstream')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def assert_inside(base, target):
    """确保 target 位于 base 之内, 防止误写原项目目录"""
    base = os.path.normcase(os.path.abspath(base))
    target = os.path.normcase(os.path.abspath(target))
    if os.path.commonpath([base, target]) != base:
        raise SystemExit('拒绝写入: %s 不在 %s 之内' % (target, base))


def sync(src, dest, apply_changes):
    missing = [n for n in SYNC_FILES if not os.path.isfile(os.path.join(src, n))]
    if missing:
        print('[!] 原项目缺少以下文件, 将跳过: %s' % ', '.join(missing))
    if not os.path.isdir(src):
        raise SystemExit('原项目目录不存在: %s' % src)

    assert_inside(dest, dest)
    changed = 0
    for name in SYNC_FILES:
        s = os.path.join(src, name)
        d = os.path.join(dest, name)
        assert_inside(dest, d)
        if not os.path.isfile(s):
            continue
        if os.path.isfile(d) and sha256(s) == sha256(d):
            continue
        changed += 1
        verb = 'UPDATE' if os.path.isfile(d) else 'CREATE'
        print('  %-6s %s' % (verb, name))
        if apply_changes:
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)
    if changed == 0:
        print('  副本已是最新, 无需改动')
    elif not apply_changes:
        print('  (dry-run: 未写入任何文件, 加 --apply 执行)')
    return changed


def verify(src, dest):
    ok = True
    for name in SYNC_FILES:
        s = os.path.join(src, name)
        d = os.path.join(dest, name)
        if not os.path.isfile(s):
            continue
        if not os.path.isfile(d):
            print('  [缺失] %s' % name)
            ok = False
            continue
        if sha256(s) != sha256(d):
            print('  [不同] %s' % name)
            ok = False
    print('校验结果: %s' % ('一致' if ok else '存在差异'))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description='同步 Better terminal (pt) 到仓库内副本')
    ap.add_argument('--src', default=DEFAULT_SRC, help='原项目目录 (只读)')
    ap.add_argument('--dest', default=DEFAULT_DEST, help='副本目录')
    ap.add_argument('--apply', action='store_true', help='真正执行复制')
    ap.add_argument('--verify', action='store_true', help='校验副本与原项目一致')
    args = ap.parse_args()

    print('原项目 (只读) : %s' % args.src)
    print('副本目录      : %s' % args.dest)
    if args.verify:
        return verify(args.src, args.dest)
    sync(args.src, args.dest, args.apply)
    return 0


if __name__ == '__main__':
    sys.exit(main())
