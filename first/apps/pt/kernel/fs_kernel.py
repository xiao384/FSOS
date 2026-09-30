# ============================================================
# fs_kernel.py - FSOS 内核文件区实现 (基于 krn 模块)
#
# 内核文件区由 C 侧 filesys.c 提供真实目录。Better Terminal 直接通过
# krn.list_files()/del_file() 访问它，从而与 C 文件管理器、编辑器共享同一批文件。
# 当前 FSOS 文件区是扁平命名空间；单文件上限由 krn_bridge/文件系统统一决定。
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
        result = krn.del_file(name)
        if isinstance(result, str) and result.startswith('ERROR'):
            raise OSError(result)
        return result

    def names(self):
        """列出内核文件区真实存在的文件。

        旧实现只读取 INDEX.TXT，导致 C 文件管理器/编辑器创建的文件在
        Better Terminal 里不可见。krn 已经提供 list_files()，这里直接复用
        内核目录作为唯一事实来源。
        """
        try:
            return list(krn.list_files())
        except Exception:
            return []
