# apps/pt — Better terminal 移植与包管理系统

把外部 `D:\better terminal_project\pt`（纯 tkinter 实现的终端程序）改造为
**一份核心 + 两个后端**，并新增**解压 / 安装 / 运行**的包管理能力，使其既能
以 PySide6 桌面程序运行，又能以内嵌方式运行在本内核（FSOS）里。

---

## 一、设计原则

1. **绝不修改原项目**。`D:\better terminal_project\pt` 只被读取。
   通过 `tools/sync_pt.py` 复制一份逐字节镜像到 `apps/pt/_upstream/` 作为对照，
   真正改造与使用的代码在本目录的 `core/ host/ kernel/ pkg/` 里。
2. **界面无关的核心**。所有 `tk.Toplevel / messagebox / wait_window` 对话框都收敛到
   `core/backend.py` 的后端接口，于是同一份业务逻辑可挂在三种后端上：
   - `kernel/backend_kernel.py` — 内核 320x200 文本终端（无 GUI）
   - `host/backend_pyside6.py` — PySide6 桌面窗口（**按你的建议，用 PySide6 取代了原 tkinter**）
   - `core/backend.py:CliBackend` — 纯命令行（无 GUI 依赖，用于测试/无界面环境）
3. **内核兼容性**。所有被打包进内核的源码（core + pkg + kernel）满足：
   - 不使用 f-string / `str.format` / `%` 格式化（MicroPython 处于 `ROM_LEVEL_MINIMUM`）；
   - 不依赖非内置模块（`os/json/platform/subprocess/time` 一律 `try/except ImportError` 探测）；
   - 解码 zip 与 base64 用本项目自带的纯 Python 实现（`pkg/inflate.py`、`pkg/b64.py`），
     因为内核 MicroPython 没有 `zlib`/`binascii`/`zipfile`。

---

## 二、目录结构

```
apps/pt/
├── _upstream/          # 原项目逐字节镜像 (只读对照, 由 sync_pt.py 生成)
├── core/               # 界面无关的核心
│   ├── config.py       # 全局状态 (config 对象) + krn/fs 插槽
│   ├── backend.py      # 后端接口 + CliBackend
│   ├── ptos.py         # SYSTEM_OS 类 (原 system_os.py 改造, 对话框走后端)
│   └── commands.py     # 命令分发 (原 commands.py 改造, 新增包管理命令)
├── pkg/                # 包格式 (宿主机/内核共用)
│   ├── b64.py          # 纯 Python base64
│   ├── inflate.py      # 纯 Python DEFLATE 解压
│   ├── tinyunzip.py    # 极简 ZIP 读取 (store / deflate + CRC)
│   ├── ptpkg.py        # install / uninstall / run / list 包管理
│   └── frozen_pkgs.py  # 内嵌包 (base64 化的 .zip, 由 bundle_pt.py 生成)
├── kernel/             # 内核侧适配
│   ├── backend_kernel.py   # 文本终端后端
│   ├── fs_kernel.py        # 基于 krn 模块的文件区
│   ├── main_kernel.py      # 内核入口 (setup/run)
│   └── help_ascii.py       # 内核 ASCII 帮助 (覆盖 core/ptos 的中文版)
├── host/               # 宿主机侧适配
│   ├── theme.py        # PySide6 版主题 + QSS (原 theme.py 改造)
│   ├── backend_pyside6.py # PySide6 对话框后端
│   ├── ui_pyside6.py   # 登录 + 终端主窗口
│   └── main_host.py    # 宿主机入口 (python main_host.py / --cli)
├── packages/           # 随内核内嵌的示例包
│   └── hello/          #   main.py + README.TXT (+ PKG.DESC 描述)
└── users_data.json     # 用户库 (与原项目同结构)
```

---

## 三、与原项目的差异（改造点）

| 原项目 (tkinter) | 本仓库 (apps/pt) |
|---|---|
| `ui.py` 用 `tk.Toplevel/after` 做登录与主窗口切换 | `host/ui_pyside6.py` 用 `QApplication/QDialog/QMainWindow`，单窗口 |
| `system_os.py` 内嵌大量 `tk` 对话框 | `core/ptos.py` 改调 `backend` 接口，业务逻辑与 GUI 解耦 |
| `theme.py` 导出 tk 小部件工厂 | `host/theme.py` 导出配色表 + QSS（`objectName` 统一套色） |
| `Auto-output` 用 `pyautogui/pyperclip` | 桌面自动化留在 `backend_pyside6.paste()`，内核/CLI 后端返回"不可用" |
| 文件操作直接 `os` | 经 `config.fs`（宿主机 `HostFS` / 内核 `KrnFS`） |
| 无包管理 | 新增 `unzip / install / pkg / run` 命令 |
| 固定 `users_data.json` 路径 | 内核走 `krn.users()`，宿主机走 `users_data.json` |

**tk → PySide6 的要点**：tkinter 的 `tk.Toplevel + wait_window` 对应 Qt 的
`QDialog.exec()`；`messagebox` 对应 `QMessageBox`；`Entry(show='*')` 对应
`QLineEdit` + `Password` 回显模式；菜单/多行编辑用 `QTextEdit`。整体更贴近
现代 Qt 风格，且避免了 tkinter 在 Windows 下"第二次 `Tk()` 窗口不显示"的坑。

---

## 四、终端命令一览

```
help / message / echo / clear / time / history
users [list|add|modify|delete]          sudo [root|<cmd>]
open <路径>  ls  pwd  cd <路径>  mkdir <路径>  rm <文件>
auto-output <1|2>                       reboot bash
theme <dark|light>                      切换终端配色 (仅宿主机 PySide6 版有效)
---- 包管理 ----
pkg list                                列出内嵌包与已安装包
pkg remove <包名>                       卸载
install <包名>                          解压并登记为已安装应用
unzip <包名>        unzip -l <包名>     解压到文件区 / 仅列出内容
run <包名>                              执行已安装应用的入口文件
```

> **配色切换**：`theme dark` / `theme light` 在内核版（ASCII 终端）仅记录偏好并提示，
> 在宿主机 PySide6 版会**立即实时换肤**。`theme.py` 提供 `dark` / `light` 两套，
> `get_theme()` 优先返回手动选定的主题（`config.theme`），未指定时跟随系统。
> 原项目的 `Better terminal_dark.py` 旧文件**不在同步清单内**，本仓库不使用它。
>

内核里 `install hello` 会从内嵌包解出 `main.py` / `README.TXT` 到文件区，
`run hello` 再执行其入口。

---

## 五、构建流程

```
# 1. (可选) 同步原项目到 _upstream/ (只读校验)
python tools/sync_pt.py --apply

# 2. 把模块化源码拼成内核可用的单文件, 并生成内嵌包 + 更新 gen_frozen_fsos.c
python tools/bundle_pt.py --frozen

# 3. 完整构建内核 (含 MicroPython), 旧 bt 终端被本版本替换
.\build-mingw.ps1 -WithPython

# 4. 重新生成 ISO (含新终端)
python pack_iso.ps1
```

测试：`python tools/test_pt.py`（34 项自检，无需任何 GUI 依赖）。

---

## 五之一、运行依赖与环境自检（关于 .venv）

原项目 (`D:\better terminal_project\pt`) 依赖 **第三方库** `pyautogui` / `pyperclip` /
`tkinter`（见 `_upstream/system_os.py`），必须用它的 `.venv` 才能跑。
**本仓库改造版不依赖这些第三方库**，因此系统裸 Python 即可运行，无需复制 `.venv`：

- **标准库**：`json / os / platform / subprocess / random / shutil / sys / time / winreg`
  —— 系统 Python 自带。
- **PySide6**：仅宿主机图形界面 (`host/`) 需要，可选；未装时内核版与纯 CLI 版照常工作。
- **pyautogui / pyperclip**：仅 `host` 的 `auto-output 2`（桌面粘贴）**函数内延迟导入**，
  缺失时返回友好提示，不影响其它功能。
- **内核版** (`pyroot/bt.py`)：由 MicroPython 执行，完全不依赖上述宿主库。

自检脚本：`python tools/check_env.py` —— 逐项报告标准库 / PySide6 / 可选库 /
拼合产物是否混入第三方 import，并给出"能否直接运行"的结论。

---

## 六、内核限制（已知约束）

- 文件区为扁平命名空间，单文件 <= 4KB，最多 16 个文件，文件名 <= 24 字符，
  且只能存文本（以首个 NUL 截断）。因此包内二进制文件无法放入。
- `pkg remove` 在内核里实际是"清空文件 + 从索引移除"（目录项仍占用一个槽位），
  因为内核 `krn` 没有删除文件的接口；宿主机 `HostFS` 走 `.trash` 回收站。
- 超过上述上限的包无法安装，install 会明确报错而不是静默截断。
