// filemgr.h - FSOS 文件管理器 (浏览/打开/新建/删除/改名 + 磁盘信息)
#ifndef FILEMGR_H
#define FILEMGR_H

void filemgr_open(void);
void filemgr_draw(int x, int y, int w, int h);
int  filemgr_key(int k);
int  filemgr_on_mouse(int mx, int my, int ldown);
void filemgr_close_ctx(void);      // wm 打开其它菜单时关闭本应用右键菜单
int  filemgr_on_rmouse(int mx, int my);  // 右键弹出上下文菜单

#endif // FILEMGR_H
