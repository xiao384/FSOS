// devstudio.h - FSOS 集成开发入口 (文件列表 + 编辑 + 保存 + 运行)
//
// 定位: 让 Python / C-C++ / Java 三种语言拥有同一个"工程文件"入口 ——
//   文件保存在磁盘文件区 (filesys), 重启不丢;
//   按扩展名自动选择运行时: .py -> Python, .c/.cc/.cpp -> C/C++, .java -> Java。
#ifndef DEVSTUDIO_H
#define DEVSTUDIO_H

// 打开窗口 / 重新扫描磁盘文件列表
void devstudio_open(void);
// 窗口内容绘制 (与 wm 的 app_t.draw 签名一致)
void devstudio_draw(int x, int y, int w, int h);
// 键盘事件 (焦点窗口时由 wm 转发); 返回 1 表示该键已被本窗口消费
int  devstudio_key(int k);
// 运行过全屏程序后, 请求 wm 忽略一次鼠标边沿 (避免误触发点击)
int  devstudio_take_skip(void);

#endif // DEVSTUDIO_H
