# ============================================================
# config.py - 全局共享运行时状态
# 由 _upstream/config.py 改造:
#   - 去掉 tk 窗口引用 (main_tk/sign_tk/tk_input_* 等), 界面状态改由各后端
#     自行持有;
#   - 增加 krn / fs 两个运行时插槽: 内核里指向 MicroPython 的 krn 模块与
#     KrnFS, 宿主机里为 None 并走 os / HostFS。
#
# 关键约束 (为内核打包服务):
#   状态集中在一个 config 对象上, 而不是散落为模块级变量。这样
#   tools/bundle_pt.py 把多个模块拼成单文件时, 只需删掉
#   "from core.config import config" 这一行, 后面的代码无需改动 ——
#   config 这个名字在拼合后的文件里依然指向同一个对象。
# ============================================================
try:
    import os as _os

    _system_name = _os.name
    # __file__ 在内核里不存在: 内核是用 mp_exec_str 执行一整段源码字符串,
    # 没有"文件"的概念。此处必须防御, 否则内核启动即在 config 处 NameError。
    try:
        _run_path = _os.path.dirname(_os.path.abspath(__file__))
    except NameError:
        _run_path = '/'
except ImportError:
    _system_name = 'pxs'
    _run_path = '/'

# 内核里存在 krn 模块 (C 侧桥接), 宿主机里没有
try:
    import krn as _krn
except ImportError:
    _krn = None


class _Config(object):
    pass


config = _Config()
config.system_name = _system_name
config.run_path = _run_path
config.cmd_path = _run_path     # 当前工作路径 (仅宿主机有意义)
config.krn = _krn               # 内核桥接模块, 宿主机为 None
config.root_permission = False  # 是否拥有 root 权限
config.users_permission = False  # 是否登录成功
config.root = None              # 当前登录的 SYSTEM_OS 实例
config.fs = None                # 文件区抽象 (KrnFS / HostFS)
