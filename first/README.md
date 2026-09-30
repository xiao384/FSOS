# FSOS v0.2

一个从零编写的 **64 位 x86-64 长模式裸机操作系统内核**，内含图形界面、用户系统、终端 shell 和 **嵌入式 MicroPython 解释器**（可执行 Python 代码）。

> 本文件是发布包的说明文档。可启动镜像为同目录下的 `image.img`（1.44MB 软盘/硬盘镜像）。

---

## 一、快速开始（QEMU）

```powershell
# 安装 QEMU 后，直接运行镜像（内核为 64 位长模式，必须用 x86_64 版 QEMU）：
qemu-system-x86_64 -drive file=image.img,format=raw,index=0,media=disk
```

启动后：
1. 登录界面默认选中 `admin`，按 **Enter** 进入密码输入
2. 输入密码 `admin`，按 **Enter** 登录
3. 进入主界面

### 登录账号

| 用户名 | 密码  | 角色  |
|--------|-------|-------|
| admin  | admin | 管理员 |
| guest  | guest | 普通用户 |

> 首次启动（或磁盘用户数据损坏时）会自动重建以上默认账号。

---

## 二、在 VMware 中运行

构建脚本已自动生成 VMware 可启动磁盘：`output/vmware/image.vmdk` + `FSOS.vmx`。

```powershell
# 一键启动（自动查找 VMware Workstation / Player）
.\run-vmware.ps1

# 或手动：VMware 中 文件 -> 打开 -> 选择 first\output\vmware\FSOS.vmx
```

> 需要 VMware Workstation 或免费版 **VMware Workstation Player**（https://www.vmware.com/products/workstation-player.html）。
> 虚拟机配置：1 vCPU、256MB 内存、IDE 硬盘引导（int13h LBA，兼容性最好）、串口输出到 `output/vmware/serial.log`。
> 每次 `.\build-mingw.ps1 -WithPython` 重建后会同步刷新 vmdk。

在 VMware 中系统使用 **真实键盘/鼠标**，登录、终端、REPL、GUI 拖动全部可用，且图形显示正常（不像 QEMU `-display none` 需要截图诊断）。

---

## 三、ISO 光盘镜像（可启动 CD / 虚拟光驱）

构建脚本产出的 `image.img` 可进一步打包成 **可启动 ISO**：

```powershell
# 用刚构建的 image.img 生成 ../iso/FSOS.iso
python make_iso.py
```

ISO 采用 **El Torito no-emulation** 引导（BIOS 直接把 ISO 扇区当成原始数据读入内存，
引导器 `iso_boot.asm` 用 int13h 扩展读 AH=0x42 把内核搬到 1MB 以上，再进入保护/长模式），
由 `pack_iso.ps1` 纯 PowerShell 手写 ISO9660 + El Torito 目录，不依赖 grub/xorriso。

### 使用方式

| 场景 | 操作 |
|------|------|
| **QEMU** | `qemu-system-x86_64 -cdrom ..\iso\FSOS.iso -boot d` |
| **VMware（图形化安装，推荐）** | 见下方"图形化安装"一节，或直接：`.\run-vmware.ps1 -Iso ..\iso\FSOS.iso` |
| **VMware（直接运行已装好的盘）** | `.\run-vmware.ps1`（需先 `.\build-mingw.ps1 -WithPython` 生成 image.vmdk） |
| **物理机** | 用 Rufus/PowerISO 等把 ISO 烧录到光盘或写入支持 El Torito 的 USB 启动盘 |

ISO 文件系统根目录含 `IMAGE.IMG`（即 image.img）、`KERNEL.BIN`、`README.TXT`。

> 构建脚本每次 `.\build-mingw.ps1 -WithPython` 重建后，需再跑一次 `.\pack_iso.ps1` 刷新 ISO。

### 图形化安装（像 Windows 安装盘一样）

ISO 启动后，若目标硬盘尚无安装标记，内核会自动进入 **图形化安装向导**
（`installer.c`，mode13h + 鼠标/键盘双输入），步骤如下：

```
欢迎页  →  [Next]
创建管理员账户 (Admin 名 + 密码, 可选 guest 账户)
         →  [Next]
计算机名 (Hostname)  →  [Next]
安装确认 (显示: 管理员 / 主机名 / 目标磁盘)
         →  [Install]
写入进度条  →  完成页: 取出安装介质, 按 ENTER 重启
```

- 交互：鼠标点击按钮/文本框，或 **Tab/方向键** 切换焦点、**Enter** 确认、**Esc** 取消（不写盘重启）。
- 安装器会把你设定的管理员 (root)、可选 guest、主机名写入镜像副本，再整体写盘，
  所以装出的系统**第一启动就带你的自定义账户**，不再有硬编码的 admin/admin。

#### 用 VMware 从 ISO 安装

```powershell
# 1) 生成一块空硬盘 + 指向 ISO 的 VMX, 并启动 VMware 进入安装向导
.\run-vmware.ps1 -Iso ..\iso\FSOS.iso
# (等价于 .\new-vm-from-iso.ps1 -Iso ..\iso\FSOS.iso)

# 2) 在图形向导里完成安装, 重启后执行:
.\set-boot-hdd.ps1          # 把启动顺序从 cdrom,hdd 切回 hdd
```

`new-vm-from-iso.ps1` 会自动探测 VMware 安装路径；找不到则只生成
`output\vmware-iso/FSOS.vmx` + 空 `FSOS.vmdk`，供你手动用 VMware 打开。

---

## 四、主界面功能

| 按键 | 功能 |
|------|------|
| `T` | 进入 **终端**（root shell） |
| `M` | 用户管理（仅管理员） |
| `G` | **GUI 桌面**（多窗口 + 鼠标拖动） |
| `P` | **任务管理器**（进程列表 / 性能 / 结束进程） |
| `L` | 锁屏/注销 |

### 终端命令

```
help      显示帮助              echo <text>   打印文本
clear     清屏                  whoami        当前用户
ver       版本信息              uptime        运行时间
meminfo   内存信息              diskinfo      磁盘信息
users     用户列表              useradd/userdel/passwd/setrole  用户管理
python    进入 MicroPython REPL（执行 Python 代码）
bt        运行 Better terminal (pt 移植版) 应用
reboot    重启系统              poweroff      关机
exit      返回主界面
```

### Python REPL（`python` 命令）

内核内嵌 **MicroPython v1.22.0**（64 位移植），支持标准 Python 语法：

```
>>> 1 + 1
2
>>> 6 * 7
42
>>> print("hello, kernel!")
hello, kernel!
```

- 按 **Ctrl+D** 退出 REPL 返回终端
- 内核通过 `krn` 模块暴露系统服务（`krn.message()`、`krn.users()` 等）

`bt` 命令运行从 `Better terminal` (D:\better terminal_project\pt) 移植的终端应用。
原项目用 tkinter 实现、与桌面自动化强耦合；本仓库把它改造成**一份核心 + 两个后端**
（详见 `apps/pt/README.md`）：宿主机侧用 **PySide6** 取代 tkinter，内核侧用文本终端后端
直接调用 `krn` 模块。新增 **解压 / 安装 / 运行** 的包管理能力：

```
bt           进入 Better terminal
pkg list     列出内嵌包与已安装包
install hello  把包解压并登记为已安装应用 (内嵌示例包 hello)
run hello      执行已安装应用的入口 main.py
unzip <包> / unzip -l <包>   解压到文件区 / 仅列出内容
pkg remove <包>               卸载
```

> 原项目 `D:\better terminal_project\pt` **从未被修改**：通过 `tools/sync_pt.py`
> 复制逐字节镜像到 `apps/pt/_upstream/` 作为对照，真正改造的代码在
> `apps/pt` 的 `core/ host/ kernel/ pkg/` 里。模块化源码由 `tools/bundle_pt.py`
> 拼成内核可用的单文件 `pyroot/bt.py` → `gen_frozen_fsos.c`。
> 端到端自检：`python tools/test_pt.py`。


### GUI 桌面（`G` 键）

简易窗口管理器：**标题栏按住左键拖动**、点击窗口聚焦（自动置顶）、实时时钟窗口。

### 任务管理器（`P` 键）

系统内置进程监控与资源管理器：

- **三栏进程分类**：
  - **系统必要进程**：内核、中断、定时器、堆管理、进程表等（不可结束）。
  - **系统非必要进程**：终端、用户管理、桌面、任务管理器自身等（可结束）。
  - **程序进程**：用户运行的 Python/C/Java 程序（可结束）。
- **性能面板**：实时 CPU 占用率（基于空闲滴答统计）、运行时长、进程总数、运行中数、内核堆用量。
- **结束进程**：选中进程后按 `K` 结束（系统必要进程拒绝并提示）。

进程模型为**单体内核 + 进程注册表**：内核启动时 `proc_init()` 注册必要/非必要系统进程，语言运行时按需启动程序进程（见下一章）。CPU 利用率通过 PIT 滴答空闲计数测算。

---

## 五、构建

需要：**MinGW gcc**（x86_64-w64-mingw32，`-m64`）、**NASM**、**python 3.x**、**mingw32-make**（MicroPython 库用）。

```powershell
# 1. 完整构建（含 MicroPython，boot + loader + kernel 拼成 image.img）
.\build-mingw.ps1 -WithPython

# 2. 不带 Python 的轻量构建（用桩替代，占位）
.\build-mingw.ps1

# 3. 构建并直接启动 QEMU
.\build-mingw.ps1 -WithPython -Run

# 4. 32 位回退构建（旧版保护模式内核）
.\build-mingw.ps1 -Arch x86
```

> 源码树包含 `micropython/`（v1.22.0 子集）。若缺失，运行 `tools/fetch_micropython.ps1` 拉取。
> `bt` 的冻结源码由 `python pyroot/gen_frozen.py` 生成（把 `pyroot/bt.py` 转成 C 字符串），构建脚本会自动调用。
> `pyroot/bt.py` 本身由 `python tools/bundle_pt.py --frozen` 从 `apps/pt` 的模块化源码拼合生成（含内嵌包与 ASCII 帮助覆盖）。修改终端请改 `apps/pt` 后重跑打包器，不要手工编辑 `pyroot/bt.py`。
> 原 Better terminal 项目 (`D:\better terminal_project\pt`) 只读，通过 `tools/sync_pt.py` 同步到 `apps/pt/_upstream/` 对照。

---

## 六、项目结构

```
first/
├── boot.asm          # 16 位引导扇区（读入二级引导 loader → 跳转）
├── loader.asm        # 二级引导（分三段读内核 → 页表 → 长模式 → 拷内核到 1MB）
├── start.asm         # 64 位入口（栈/BSS/SSE 初始化 → kernel_main）
├── kernel.c          # 内核主函数（初始化 → GUI）
├── idt.c/.asm        # IDT/PIC/PIT + 异常处理（dump 后停机，MS x64 ABI 传参）
├── vga.c             # mode 13h 图形驱动（320x200x256）
├── kb.c / mouse.c    # PS/2 键盘 / 鼠标中断驱动（含 Shift/Ctrl 状态跟踪）
├── ata.c             # ATA PIO 磁盘读写
├── user.c            # 用户账户（磁盘持久化）
├── gui.c / wm.c      # 图形组件 / 简易窗口管理器
├── app.c             # 登录 / 主界面 / 用户管理
├── terminal.c        # root shell（含 python/bt 命令）
├── mp_port/          # MicroPython 内核侧入口（桩/接口）
├── pyroot/bt.py      # Better terminal 移植版 Python 源码
│   └── gen_frozen.py # bt.py → gen_frozen_fsos.c 的转换脚本
└── micropython/ports/fsos/   # MicroPython 裸机端口（setjmp/chkstk 用 NASM）
```

### 引导内存布局（loader.asm）

| 区域 | 用途 |
|------|------|
| `0x500-0x6000` | 内核第二段低区（SeaBIOS int13h 可靠写入） |
| `0x7E00-0x8E00` | loader 自身 |
| `0x9000-0xC000` | 页表（PML4/PDPT/PD，2MB 大页映射前 1GB） |
| `0xC000-0xFFFF` | 内核第二段高区 |
| `0x10000-0xA0000` | 内核第一段（bin 偏移 0-0x8FFFF，576KB） |
| `0xB0000` | 8x8 VGA 字体（mode13h 不用的高 64KB 显存） |

---

## 七、已知限制

- 教学性质：无用户态隔离，密码明文存储
- 多用户为"仿真"：角色只控制 GUI 功能开关，终端固定 root
- ATA 仅主通道 LBA28、PIO 轮询，无 DMA
- MicroPython 字符串 `%` 格式化未启用（实现约 44KB，超出引导低端缓冲上限），`bt` 已改用字符串拼接
- 终端/REPL 中文输入不受支持（PS/2 键盘映射为 ASCII）

---

## 八、技术亮点

- 自定义 BIOS 引导扇区 + 二级引导 loader，**分三段**加载大内核（避开 VGA 显存/页表/loader 区）
- 引导扇区**分批读取**（BIOS 单次最多 127 扇区），int13h 破坏寄存器时用内存变量保存进度
- 64 位长模式：PML4/PDPT/PD 三级页表，2MB 大页 identity 映射前 1GB
- 中断/异常：IDT 16 字节门，`idt_asm.asm` 严格按 MS x64 ABI 传参 + shadow space
- 异常处理：CPU 异常 dump 寄存器现场（串口 + 屏幕）后停机，不再静默
- MicroPython 嵌入式：`-Os` + `--gc-sections` + 紧凑链接布局，内核 610KB（引导低端缓冲容量内）
- 定制 `setjmp/longjmp`（NASM）、`__chkstk_ms`（NASM），适配裸机 + 长模式

## Modern Desktop v14

The GUI shell now follows a native-resolution design instead of treating 320x200 as the desktop canvas.

- UEFI GOP is the primary desktop framebuffer and prefers 1920x1080 when the firmware exposes that mode.
- The shell uses physical screen coordinates for the desktop, windows, dock, launcher, and right-side control center.
- Native UI CJK text uses the 24x24 glyph table at high-resolution scale and the legacy 16x16 table on smaller displays.
- Theme colors use a dedicated native-GOP RGB token namespace so the 32bpp UI does not inherit the old VGA palette saturation.
- The right-side control center is expandable/collapsible and keeps separate power actions.
- Start menu provides separate `注销` and `关机` actions.
- Terminal `poweroff`/`reboot` remain available.

Build on Windows from `first` using the existing clean VMware script after installing the project's toolchain:

```powershell
.\tools\build_modern_vmware_v9.ps1
```

The generated VMware files are under `output\Auto`.
