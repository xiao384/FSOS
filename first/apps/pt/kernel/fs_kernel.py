# ============================================================
# fs_kernel.py - FSOS 内核文件区实现 (基于 krn 模块)
#
# 内核没有目录树, krn 只提供扁平文件区:
#   krn.read_file(name)        -> str   (以首个 NUL 截断, 故只能存文本)
#   krn.write_file(name, text) -> str   (单文件上限 4KB, 最多 16 个文件)
#   krn 没有"列目录"与"删除文件"接口, 因此:
#     - names() 读 pt 自己维护的 INDEX.TXT 索引;
#     - delete() 把内容清空并从索引移除 (目录项保留但内容为空)。
#
# 这是刻意的取舍: 不改内核 C 代码 (krn_bridge.c) 就能支持安装,
# 代价是"已删除"的文件仍占用一个目录槽位。
# ============================================================
import krn
from pkg.ptpkg import parse_index, INDEX_NAME


class KrnFS(object):
    name = 'krn'

    # ---- 路径: 内核是扁平命名空间, 原样返回 ----
    def path(self, name):
        return name

    def cwd(self):
        return '/'

    def chdir(self, name):
        return '内核文件区没有目录概念, 不支持 cd'

    def mkdir(self, name):
        return '内核文件区没有目录概念, 不支持 mkdir'

    def isdir(self, name):
        return False

    # ---- 文件 ----
    def exists(self, name):
        try:
            krn.read_file(name)
            return True
        except Exception:
            return False

    def read(self, name):
        return krn.read_file(name)

    def write(self, name, text):
        return krn.write_file(name, text)

    def delete(self, name):
        try:
            krn.write_file(name, '')
        except Exception:
            pass
        return '已清空: ' + name

    def names(self):
        """列出 pt 已安装的文件 (来自 INDEX.TXT)"""
        try:
            text = krn.read_file(INDEX_NAME)
        except Exception:
            return []
        out = []
        for rec in parse_index(text):
            for f in rec['files']:
                out.append(f)
        return out
