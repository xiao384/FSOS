// icon.h - 应用图标加载与绘制
//
// 图标格式 (磁盘文件区, 与 filesys.c 兼容):
//   offset 0: 'I','C','N','1' (magic, 4B)
//   offset 4: uint16 width
//   offset 6: uint16 height
//   offset 8: uint8 pixels[width*height]  (COL_* 调色板索引)
//
// 双路径: icon_draw() 从磁盘加载; 失败回退 icon_draw_builtin() 程序化绘制。
#ifndef ICON_H
#define ICON_H

#include <stdint.h>

#define ICON_SIZE 32           // 图标尺寸 32x32

// 内嵌图标 ID (程序化绘制, 磁盘无文件时回退)
#define ICON_TERMINAL   0      // 终端
#define ICON_USERMGR    1      // 用户管理
#define ICON_DESKTOP    2      // 桌面
#define ICON_TASKMGR    3      // 任务管理器
#define ICON_LOCK       4      // 锁屏/注销
#define ICON_SOFTWARE   5      // 软件/包管理
#define ICON_COUNT      6

// 从磁盘加载图标文件并绘制到 (x, y)。成功返回 1, 失败返回 0。
int icon_draw(const char* name, int x, int y);

// 程序化绘制内嵌图标 (id 见 ICON_*)。返回 1。
int icon_draw_builtin(int id, int x, int y);

// 便捷: 先试磁盘加载 (name), 失败回退内嵌 (id)。
int icon_draw_auto(const char* name, int id, int x, int y);

#endif // ICON_H