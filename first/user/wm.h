// wm.h - 简易窗口管理器演示入口
#ifndef WM_H
#define WM_H

// 运行桌面演示 (多窗口, 鼠标拖动/聚焦), ESC 或 Q 返回
void wm_demo_run(void);
// 以 act 启动一个窗口应用 (供其它模块在内部拉起, 如文件管理器打开编辑器)
void wm_launch_app(int act);

#endif // WM_H
