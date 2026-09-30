#ifndef FSOS_SIDEBAR_H
#define FSOS_SIDEBAR_H

// 右上角系统浮动面板：展开/收起、绘制与命中测试。
void sidebar_init(void);
void sidebar_set_open(int open);
int  sidebar_is_open(void);
void sidebar_toggle(void);
void sidebar_draw(int mx, int my);
// 返回 1 表示鼠标点击被侧边栏消费；0 表示未命中。
int  sidebar_on_click(int mx, int my);
// 侧边栏入口（收起时的小按钮）的矩形。
void sidebar_toggle_rect(int* x, int* y, int* w, int* h);

#endif
