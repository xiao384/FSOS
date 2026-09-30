# FSOS UI / Runtime First-Principles Fix — v23

本版本直接针对实际 VMware 截图中的四类问题改造，并把 UI、输入、窗口生命周期、语言运行时的职责重新对齐。

## 1. 鼠标渲染与输入

问题根因不是“把光标画得更漂亮”这么简单：高分辨率 GOP、VMware 绝对鼠标、PS/2 相对鼠标和 WM 光标绘制之间必须共享同一套屏幕坐标。

改动：
- native GOP 模式下 X/Y 都使用 1:1 的设备计数灵敏度，避免 1920×1080 下垂直/水平不同速。
- VMware 绝对坐标使用 0..0xFFFF 的完整范围映射到实际 framebuffer，并先做屏幕尺寸检查。
- WM 光标改成黑描边 + 白主体的高分辨率箭头，并允许在整个屏幕（包括 Dock/任务栏）内显示。
- 保持 WM 的“鼠标移动 -> 全局重绘 -> gfx_flip”单一渲染路径，避免旧光标残留。

## 2. 编译器 / 开发工具

问题根因是旧 DevStudio 仍按 320×200 / 固定 8px 单元绘制，且 C 示例不是可执行的 `main()` 入口；语言运行也没有统一的输出面板。

改动：
- DevStudio 使用窗口实际尺寸计算代码区、侧栏、状态栏、输出区和可见行数。
- 增加文件侧栏和鼠标点击选择。
- Ctrl+R 的 Python/C/C++/Java 输出回收到开发窗口底部输出面板，不再覆盖整个桌面。
- C 新建模板包含 `int main()` 和 `return 0;`。
- C/C++、Java 模块加入协作式执行时间预算；解释器循环超时后向 IDE 返回失败，而不是永久堵塞桌面。

## 3. 注销 -> 登录 -> 桌面

问题根因是登录成功后仍会再次进入旧的 `screen_main()` 路径，而且旧窗口状态/焦点没有完整释放。

改动：
- 登录成功后统一进入 `wm_demo_run()` 现代桌面。
- 每个桌面 session 开始时关闭上一个 session 的所有应用并清零焦点、z-order、拖动、开始菜单、控制中心、桌面选中状态。
- 注销后下一轮直接显示登录界面；重新登录后不会回退到旧版 320×200 菜单。

## 4. 终端

问题根因是终端同时存在“真正的 WM 窗口”和旧的固定大小文本控制台模型，导致高分辨率窗口只有左上角一小块真正使用。

改动：
- 终端作为标准 WM app，统一使用 draw/key/mouse/tick/close 生命周期。
- 输出区根据窗口实际尺寸动态计算字体单元、行数、列数。
- 增加标题栏、状态/提示区、输入区和滚动输出区。
- 运行 `.py/.c/.cpp/.java` 时把 stdout 导入终端历史，不再清屏后等待按键。
- Python `mp_fsos_run_str()` 去掉“Press any key to return to desktop”阻塞行为。

## 显示文字一致性

- 修正 native GOP 高分辨率下 ASCII 字符绘制尺寸与 CJK 字符宽度不一致的问题。
- `vga_text_w()` 与实际字体缩放保持一致，避免标题/菜单居中位置错误。

## 验证

- Python 单元 / 静态回归测试：60 项通过。
- 关键修改过的 C 源文件：GCC `-fsyntax-only` 全部通过（vga/mouse/cjk/module/CINT/JVM/DevStudio/WM/window/terminal/app）。
- MicroPython `mp_entry.c` 在当前工作环境无法独立做完整语法编译，因为项目尚未生成 MicroPython 的 `genhdr/qstrdefs.generated.h`；这是构建生成物缺失，不是本次修改产生的编译错误。
- 当前环境缺少 NASM/QEMU，因此没有在本容器内完成完整 FSOS/UEFI/VMware 启动测试；不要把本包描述成“已在 VMware 实机验证”。
