# FSOS GUI Concept v9

本版本针对 VMware 实机截图反馈，修复 GUI 字体路径并明确“源码与 VM 二进制”的边界。

## 核心修复

- 桌面/窗口/任务栏/开始菜单等 GUI 文本统一从旧 `cjk_ui_*` 8x8 压缩路径迁移到完整 `cjk_text*`。
- 中文 UI 使用 `cjk_bitmaps16` 的完整 16x16 字形；ASCII 使用 8x8 字形并在 16px 行高中垂直居中。
- 保留 `cjk_ui_*` API 兼容，但它现在也使用完整中文 16x16 渲染。
- 新增 GUI/font 回归测试，防止以后再次恢复成 8x8 中文压缩。
- 新增 `tools/build_modern_vmware_v9.ps1`：先跑测试，再 clean build、UEFI、VMware 打包，避免旧 kernel 混入 VMware 镜像。

## 为什么之前 VMware 看起来没变化

之前提供的 VMware 镜像使用的是历史预编译 kernel；源码的 GUI 改动没有重新编译进入该 kernel，因此 VMware 中看到的仍然是旧 320x200 像素桌面。这不是视觉参数不够，而是二进制版本不一致。

## 验证

- 44/44 Python 自动化测试通过。
- `user/cjk.c` freestanding 语法检查通过。
- 当前执行环境没有 NASM/MinGW/QEMU，因此没有把当前 v9 源码重新编译成新的 VMware kernel；避免再次提供“名字更新、内核旧”的镜像。
