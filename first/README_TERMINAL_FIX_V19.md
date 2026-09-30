# FSOS Terminal Fix v19

本版本修复桌面“终端”入口无响应/不明显的问题，并让 Better Terminal 适配实际 GOP 分辨率。

## 行为

- 桌面“终端”图标：单击即可启动，不再强制双击。
- 其它桌面图标：继续保持双击启动。
- Better Terminal：通过 `krn.screen_size()` 获取真实 framebuffer 尺寸。
- 终端字符网格自动按 8x8 字体计算列/行数，支持 1920x1080（240x135 字符格）。
- 输出按当前列宽自动换行，避免只绘制左上角 320x200 内容。

## 构建

重新 clean build 后由现有 VMware 构建脚本制作镜像。
