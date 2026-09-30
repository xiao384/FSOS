#!/usr/bin/env python3
"""detect_host.py - 平台事实快照命令行工具 (cross_platform 任务 1.3)

默认输出人类可读平台事实 (OS 家族 / NASM 格式 / ld 模拟 / 工具路径)，--json 输出
结构化结果供 CI 与 checkenv 复用。
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description="FSOS 宿主平台事实快照")
    ap.add_argument("--json", action="store_true", help="输出 JSON 结构化平台事实")
    args = ap.parse_args(argv)

    facts = detect()
    inject = new(facts)

    if args.json:
        out = dict(facts.as_dict(), inject=inject.as_dict())
        print(json.dumps(out, indent=2, ensure_ascii=False))
        return 0

    print("OS family     : %s" % facts.family)
    print("arch bits     : %d" % facts.arch_bits)
    print("path separator: %r" % facts.path_sep)
    print("NASM format   : %s" % inject.asm_fmt)
    print("NASM defs     : %s" % (" ".join(inject.asm_defs) or "-"))
    print("NASM dbg      : %s" % " ".join(inject.nasm_dbg))
    print("ld emulation  : %s" % inject.ld_emu)
    print("objcopy remove: %s" % (" ".join(inject.objcopy_remove) or "-"))
    print("shell 契约    : %s" % inject.shell)
    print("tools:")
    for name, path in sorted(facts.tools.items()):
        print("  %-9s %s" % (name, path or "(missing)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())