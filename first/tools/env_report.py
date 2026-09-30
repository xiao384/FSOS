#!/usr/bin/env python3
"""env_report.py - 宿主工具链四要素自检报告 (cross_platform 任务 6.1/6.2)

检测 NASM / C/C++ 编译器 / Python 3 / QEMU 的:
  1. 存在性 (OK/FAIL)
  2. 版本 (低于最低要求标记"建议升级")
  3. 可用性 (编译器最小 freestanding 编译探测)
  4. 补齐命令 (winget/brew/apt/dnf/pacman)
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from host_platform import detect, new  # noqa: E402
from build_config import BuildConfig  # noqa: E402


def _parse_version(text):
    """从命令输出中提取版本号 x.y.z 或 x.y。"""
    m = re.search(r"(\d+\.\d+(?:\.\d+)?)", text)
    return m.group(1) if m else None


def _cmp_version(a, b):
    """比较版本号, 返回 -1/0/1。"""
    ta = tuple(int(x) for x in a.split("."))
    tb = tuple(int(x) for x in b.split("."))
    return (ta > tb) - (ta < tb)


def _run_version(tool, args):
    try:
        cp = subprocess.run([tool] + args, capture_output=True, text=True, timeout=10)
        return (cp.stdout or "") + (cp.stderr or "")
    except Exception:
        return ""


def check_tool(name, names, min_ver, facts):
    """检查单个工具: 存在性 + 版本。返回 dict。"""
    path = facts.tools.get(name) or facts.resolve_tool(names)
    result = {"name": name, "path": path, "ok": bool(path), "version": None,
              "needs_upgrade": False}
    if path and min_ver:
        text = _run_version(path, ["--version"])
        ver = _parse_version(text)
        result["version"] = ver
        if ver and _cmp_version(ver, min_ver) < 0:
            result["needs_upgrade"] = True
    return result


def check_compiler_usability(facts, inject):
    """编译器最小 freestanding 编译探测: 确认可生成目标格式。"""
    cc = facts.tools.get("cc") or facts.resolve_tool(BuildConfig.CC_NAMES)
    if not cc:
        return False
    import tempfile
    src = tempfile.NamedTemporaryFile(mode="w", suffix=".c", delete=False)
    src.write("int _start() { return 0; }\n")
    src.close()
    obj = src.name + ".o"
    try:
        argv = [cc, "-ffreestanding", "-nostdlib", "-c", "-o", obj, src.name]
        cp = inject.shell_cmd(argv, capture_output=True)
        ok = cp.returncode == 0 and os.path.isfile(obj)
        return ok
    except Exception:
        return False
    finally:
        for f in (src.name, obj):
            try:
                os.unlink(f)
            except OSError:
                pass


_INSTALL_HINTS = {
    "windows": {
        "nasm": "winget install nasm",
        "cc": "winget install mingw",
        "cxx": "winget install mingw",
        "python": "winget install Python.Python.3.12",
        "qemu": "winget install SoftwareFreedomConservancy.QEMU",
    },
    "macos": {
        "nasm": "brew install nasm",
        "cc": "brew install gcc",
        "cxx": "brew install gcc",
        "python": "brew install python3",
        "qemu": "brew install qemu",
    },
    "linux": {
        "nasm": "sudo apt install nasm  # 或 sudo dnf install nasm / sudo pacman -S nasm",
        "cc": "sudo apt install gcc  # 或 sudo dnf install gcc / sudo pacman -S gcc",
        "cxx": "sudo apt install g++  # 或 sudo dnf install gcc-c++ / sudo pacman -S gcc",
        "python": "sudo apt install python3  # 或 sudo dnf install python3",
        "qemu": "sudo apt install qemu-system-x86  # 或 sudo dnf install qemu-kvm",
    },
}


def install_hint(family, name):
    return _INSTALL_HINTS.get(family, _INSTALL_HINTS["linux"]).get(name, name)


def generate_report(json_output=False):
    """生成工具链自检报告。返回 (text, all_ok)。"""
    facts = detect()
    inj = new(facts)
    tools = [
        ("nasm", BuildConfig.NASM_NAMES, BuildConfig.NASM_MIN),
        ("cc", BuildConfig.CC_NAMES, BuildConfig.GCC_MIN),
        ("cxx", BuildConfig.CXX_NAMES, BuildConfig.GCC_MIN),
        ("python", ["python", "python3"], BuildConfig.PYTHON_MIN),
        ("qemu", BuildConfig.QEMU_NAMES, BuildConfig.QEMU_MIN),
    ]
    results = []
    for name, names, min_ver in tools:
        results.append(check_tool(name, names, min_ver, facts))

    cc_usable = check_compiler_usability(facts, inj)

    if json_output:
        import json
        data = {
            "host": "%s/%d" % (facts.family, facts.arch_bits),
            "tools": results,
            "cc_usable": cc_usable,
        }
        return json.dumps(data, indent=2), all(r["ok"] for r in results)

    lines = ["[checkenv] host: %s/%d (shell=%s)" %
             (facts.family, facts.arch_bits, inj.shell)]
    missing = []
    for r in results:
        if r["ok"]:
            tag = "[OK]"
            extra = ""
            if r["version"]:
                extra = " v%s" % r["version"]
                if r["needs_upgrade"]:
                    extra += " (建议升级, 最低 %s)" % next(
                        v for n, _, v in tools if n == r["name"])
            lines.append("  %s %-8s %s%s" % (tag, r["name"], r["path"], extra))
        else:
            tag = "[MISSING]"
            lines.append("  %s %-8s" % (tag, r["name"]))
            missing.append(r["name"])

    if not cc_usable and any(r["name"] == "cc" and r["ok"] for r in results):
        lines.append("  [WARN] cc       freestanding 编译探测失败")

    if missing:
        lines.append("  补齐命令:")
        for m in missing:
            lines.append("    %s: %s" % (m, install_hint(facts.family, m)))

    all_ok = not missing
    lines.append("[checkenv] %s" % ("all toolchain present" if all_ok else "toolchain incomplete"))
    return "\n".join(lines), all_ok


def main():
    import argparse
    ap = argparse.ArgumentParser(description="FSOS 工具链四要素自检")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    text, ok = generate_report(json_output=args.json)
    print(text)
    return 0 if ok else 4


if __name__ == "__main__":
    raise SystemExit(main())