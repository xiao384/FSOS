# FSOS v24 鼠标 Dock 残影修复

## 根因
桌面合成器每次鼠标移动都会进行全屏合成，但 `desktop_draw_wallpaper()` 原先只绘制到 `WM_TASKBAR_Y`。
Dock 区域下方没有 root-scene 背景覆盖，因此上一帧鼠标指针写入的像素可能残留在下一帧，表现为鼠标移动到 Dock 附近时出现长影/拖影。

## 修复
`first/user/shell.c`：
- 根场景壁纸从 `H=WM_TASKBAR_Y` 改为 `H=SCREEN_H`。
- Dock 仍由 taskbar 图层后绘制，不改变布局。

这样每次完整合成都会先覆盖整个 framebuffer，再绘制 Dock、窗口和鼠标指针，旧指针位置会被确定性擦除。

## 回归测试
新增 `test_wallpaper_covers_full_frame_below_dock`。
完整测试集：61 项通过。
