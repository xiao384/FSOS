// vscode.h - "开发"软件的 VSCode 模式入口
//
// 本系统内的 DevStudio(内置 IDE) 可整体切换为 VSCode:
//   VSCode 实际运行在宿主机 (Windows), 用于编辑本操作系统的全部源码
//   (first/ 下的内核与模块). 本窗口负责说明并作为切换入口。
#ifndef VSCODE_H
#define VSCODE_H

void vscode_open(void);
void vscode_draw(int x, int y, int w, int h);
int  vscode_key(int k);                 // 返回 1 表示已消费
int  vscode_take_skip(void);            // VSCode 不启动全屏程序, 恒为 0

#endif // VSCODE_H
