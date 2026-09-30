#!/usr/bin/env python3
"""layout_parse.py - layout.h 统一布局解析器 (cross_platform 任务 2.4)

将 first/layout.h 的 #define 常量解析为 LayoutMap, 供构建拼装、打包、校验三处复用,
消除手工粘贴 LBA/扇区数值的失同步风险。要点:
  - UTF-8 整体读取 + 全文正则匹配 (规避 Windows PowerShell 默认 ANSI 解码行错乱问题)
  - 解析失败即中止 (与存量 build-mingw.ps1 行为一致)
  - 提供磁盘布局重叠校验
"""
import os
import re

_DEFINE_RE = re.compile(r"^\s*#define\s+(\w+)\s+(\d+)", re.MULTILINE)


class LayoutError(Exception):
    pass


class LayoutMap:
    """layout.h 常量映射, 属性访问与 dict 访问均可。"""

    _REQUIRED = ('DISK_SECTORS', 'LBA_USER_SB', 'LBA_USER_REC', 'LBA_FS_DIR',
                 'LBA_FS_DATA', 'LBA_SYSCONF', 'LBA_MOD_CINT', 'LBA_MOD_JVM',
                 'LBA_KERNEL', 'LBA_LOADER', 'LBA_BOOT', 'MOD_REGION_SECTORS')

    def __init__(self, values):
        missing = [k for k in self._REQUIRED if k not in values]
        if missing:
            raise LayoutError("layout.h missing #define(s): %s" % ", ".join(missing))
        self._v = dict(values)

    def __getitem__(self, key):
        return self._v[key]

    def __getattr__(self, key):
        if key in self._v:
            return self._v[key]
        raise AttributeError(key)

    def get(self, key, default=None):
        return self._v.get(key, default)

    def as_dict(self):
        return dict(self._v)

    @property
    def kernel_end_lba_estimate(self):
        """估算内核结束 LBA (由调用方用真实扇区数覆盖), 此处用第 0 硬盘约定:
        构建方在拼装后回调 seal_kernel_end() 注入真实值。"""
        return self._v.get('_kernel_end', None)

    def seal_kernel_end(self, lba):
        self._v['_kernel_end'] = lba

    # ------------------------------------------------------------------
    # 布局重叠校验: boot/loader/kernel/各数据区/模块窗口互不相交
    # ------------------------------------------------------------------
    def validate_overlap(self, kernel_end_lba=None):
        end = kernel_end_lba if kernel_end_lba is not None else self._v.get('_kernel_end')
        regions = [
            ("boot", self.LBA_BOOT, 1),
            ("loader", self.LBA_LOADER, 8),
            ("kernel", self.LBA_KERNEL, max(end - self.LBA_KERNEL, 1) if end else 1),
            ("user db sb", self.LBA_USER_SB, 1),
            ("user db rec", self.LBA_USER_REC, 1),
            ("krn fs dir", self.LBA_FS_DIR, 4),
            ("krn fs data", self.LBA_FS_DATA, 2048),
            ("sysconf", self.LBA_SYSCONF, 4),
            ("mod cint", self.LBA_MOD_CINT, self.MOD_REGION_SECTORS),
            ("mod jvm", self.LBA_MOD_JVM, self.MOD_REGION_SECTORS),
        ]
        for i in range(len(regions)):
            name_a, lba_a, secs_a = regions[i]
            for j in range(i + 1, len(regions)):
                name_b, lba_b, secs_b = regions[j]
                if not (lba_a + secs_a <= lba_b or lba_b + secs_b <= lba_a):
                    raise LayoutError(
                        "layout overlaps: %s (LBA %d+%d) vs %s (LBA %d+%d)"
                        % (name_a, lba_a, secs_a, name_b, lba_b, secs_b))
        return True

    def __repr__(self):
        return "LayoutMap(%s)" % ", ".join("%s=%d" % (k, self._v[k])
                                           for k in sorted(self._v) if k != '_kernel_end')


def parse_layout_text(text):
    """解析 layout.h 文本 (UTF-8 decode str), 返回 LayoutMap。"""
    values = {}
    for m in _DEFINE_RE.finditer(text):
        name, num = m.group(1), int(m.group(2))
        values.setdefault(name, num)   # 首个定义生效 (避免重复定义覆盖)
    return LayoutMap(values)


def load_layout(path=None):
    """读取并解析 first/layout.h。path 缺省时定位 first/ 目录。"""
    if path is None:
        here = os.path.dirname(os.path.abspath(__file__))
        path = os.path.normpath(os.path.join(here, "..", "layout.h"))
    if not os.path.isfile(path):
        raise LayoutError("layout.h not found: %s" % path)
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    return parse_layout_text(text)


if __name__ == "__main__":
    lm = load_layout()
    print(lm)
    print("DISK_SECTORS =", lm.DISK_SECTORS, "LBA_MOD_CINT =", lm.LBA_MOD_CINT)
    assert lm.LBA_MOD_CINT == 4000000 and lm.LBA_MOD_JVM == 6097152
    lm.validate_overlap()
    print("overlap check: OK")