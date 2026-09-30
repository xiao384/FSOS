// editor.h - FSOS 原生 VSCode 风格代码编辑器 (不依赖 Electron)
//
// 提供: 多标签页、文件资源管理器侧边栏、语法高亮、命令面板(Ctrl+Shift+P)、
// 快速打开(Ctrl+P)、鼠标交互(标签/侧边栏/文本定位)、闪烁光标、暗色主题。
// 以 C 编译进内核, 与 wm 的 app_t 回调契约一致。
#ifndef EDITOR_H
#define EDITOR_H

#include <stdint.h>

// 打开窗口 / 重新扫描磁盘文件列表
void editor_open(void);
// 打开指定文件到新标签 (供文件管理器等外部调用)
void editor_open_file(const char* name);
// 打开指定目录(dir=LBA_FS_DIR 为根)下的文件到新标签
void editor_open_file_in(const char* name, uint32_t dir);
// 窗口内容绘制 (与 wm 的 app_t.draw 签名一致)
void editor_draw(int x, int y, int w, int h);
// 键盘事件 (焦点窗口时由 wm 转发); 返回 1 表示该键已被本窗口消费
int  editor_key(int k);
void editor_close(void);
// 重置编辑器状态；窗口关闭时调用。
void editor_close(void);
// 鼠标事件 (wm 在点击落到本窗口客户区时转发): x,y 为屏幕坐标, ldown=1 表示按下边沿
int  editor_on_mouse(int x, int y, int ldown);
// 周期性回调: 返回 1 表示本窗口需要重绘(用于光标闪烁)
int  editor_tick(void);
// 运行过全屏程序后, 请求 wm 忽略一次鼠标边沿
int  editor_take_skip(void);

#endif // EDITOR_H
