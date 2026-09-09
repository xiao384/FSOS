# ============================================================
# config.py — 全局共享运行时状态模块
# 保存各模块之间需要共享的变量与常量
# ============================================================
import os

# 系统检测
'''以下变量不可修改'''
__system_name__ = os.name
__run_path__ = os.path.dirname(os.path.abspath(__file__))

# 运行时全局状态
cmd_path = __run_path__
root_permission = False        # 是否拥有 root 权限
users_permission = False       # 是否登录成功
root = None                    # 当前登录的 SYSTEM_OS 实例
main_tk = None                 # 终端主窗口
sign_tk = None                 # 登录窗口
tk_input_name = None           # 登录窗口-用户名输入框
tk_input_key = None            # 登录窗口-密码输入框
return_text_tk = None          # 终端输出文本区（供 clear 命令清空）
cmd_history = []               # 历史命令（完整输入行，供 ↑/↓ 回溯与 history 命令）
sudo_state = None              # sudo 密码输入状态：None 或 {'mode': 'root'|'run', 'command_text': str, 'attempts': int}
