// disktool.h - FSOS 磁盘工具 (磁盘几何 / 逻辑分区 / 格式化)
#ifndef DISKTOOL_H
#define DISKTOOL_H

void disktool_open(void);
void disktool_draw(int x, int y, int w, int h);
int  disktool_key(int k);
int  disktool_on_mouse(int mx, int my, int ldown);

#endif // DISKTOOL_H
