# FSOS UI Polish v22

本版以第一性原则重新整理桌面视觉层：

- 以 UEFI/VBE 实际帧缓冲分辨率作为桌面画布，不再把 320x200 作为 UI 设计基准。
- 统一深色 Aurora 视觉令牌：更深的蓝黑背景、更清晰的蓝色强调、更克制的边框与悬停层。
- 桌面图标改为 64px 大图标卡片，Dock / Start Menu 使用统一的大图标系统。
- 顶栏改为胶囊式品牌/时间/网络/控制中心布局。
- Dock 增加更明确的活动指示器、运行应用区域与时间胶囊。
- 窗口标题栏与控制按钮加大，并统一圆角/阴影层级。
- 开始菜单扩大并重新平衡搜索、固定应用和底部电源区。
- 控制中心尺寸与间距重新调整。
- 修复窗口关闭按钮颜色查询使用错误 token 的 UI bug。
- 保留原有应用逻辑与 WM/app_t 生命周期接口，重点改动视觉与布局层。

验证：
- GUI compiler tests: 通过
- GUI font contract tests: 通过
- app close tests: 通过
- terminal app tests: 通过
- Python 工具脚本语法检查: 通过

VMware/UEFI 打包仍使用项目原有工具链；本环境未运行 VMware 实机。
