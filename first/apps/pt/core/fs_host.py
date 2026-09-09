# ============================================================
# fs_host.py - 宿主机文件区实现 (基于 os)
#
# 与 kernel/fs_kernel.py 实现同一套接口, 使 core/ptos.py 的文件命令
# (ls/pwd/cd/mkdir/rm/open) 无需区分运行在宿主机还是内核。
# ============================================================
import os
import shutil


class HostFS(object):
    name = 'host'

    def __init__(self, root=None):
        self.root = os.path.abspath(root or os.getcwd())
        self.cwd_path = self.root
        # 回收站: 与内核侧的不可恢复删除区分, 宿主机仍走 .trash 目录
        self.trash = os.path.join(self.root, '.trash')

    # ---- 路径 ----
    def path(self, name):
        if os.path.isabs(name):
            return os.path.abspath(name)
        return os.path.abspath(os.path.join(self.cwd_path, name))

    def cwd(self):
        return self.cwd_path

    def chdir(self, name):
        target = self.path(name)
        if not os.path.isdir(target):
            return '路径不存在或不是目录: ' + target
        self.cwd_path = target
        return '工作路径已切换为: ' + target

    def mkdir(self, name):
        target = self.path(name)
        if os.path.exists(target):
            return '目录已存在: ' + target
        try:
            os.makedirs(target)
        except Exception as e:
            return '创建目录失败: ' + str(e)
        return '目录已创建: ' + target

    # ---- 文件 ----
    def names(self):
        try:
            return sorted(os.listdir(self.cwd_path))
        except OSError as e:
            return []

    def exists(self, name):
        return os.path.exists(self.path(name))

    def read(self, name):
        with open(self.path(name), 'r', encoding='utf-8') as f:
            return f.read()

    def write(self, name, text):
        target = self.path(name)
        os.makedirs(os.path.dirname(target) or '.', exist_ok=True)
        with open(target, 'w', encoding='utf-8') as f:
            f.write(text)

    def isdir(self, name):
        return os.path.isdir(self.path(name))

    def delete(self, name):
        """移入 .trash 目录, 不做不可恢复删除"""
        target = self.path(name)
        if not os.path.exists(target):
            return '路径不存在: ' + target
        os.makedirs(self.trash, exist_ok=True)
        dest = os.path.join(self.trash, os.path.basename(os.path.normpath(target)))
        if os.path.exists(dest):
            dest = dest + '.1'
        try:
            shutil.move(target, dest)
        except Exception as e:
            return '删除失败: ' + str(e)
        return '已删除 (移入回收站): ' + target
