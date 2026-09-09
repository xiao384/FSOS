// driver.h - 统一系统驱动管理
// 把分散的硬件初始化 (vga/kb/mouse/ata/serial/idt/pic/pit) 收敛到一张驱动表,
// 支持: 启动期自动初始化全部内置驱动、运行时查询状态、root 动态注册新驱动。
// 注意: 内置驱动属于内核镜像(核心), 受布局守卫保护不可被写坏;
//       root 只能动态注册"额外"驱动, 不能改动/覆盖核心驱动本身。
#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

typedef enum {
    DRV_DISPLAY = 0,   // 显示
    DRV_INPUT,         // 输入 (键鼠)
    DRV_BLOCK,         // 块设备 (磁盘)
    DRV_CHAR,          // 字符设备 (串口)
    DRV_BUS,           // 总线/中断控制
    DRV_MISC           // 杂项 (计时器等)
} drv_type_t;

#define DRV_MAX 32

typedef struct driver {
    const char* name;
    drv_type_t  type;
    int         (*init)(void);   // 初始化, 返回 0 成功
    const char* desc;
    int         status;          // 0=未加载, 1=已激活, -1=失败
    int         builtin;         // 1=内核内置(核心), 0=root 动态注册
} driver_t;

// 注册并自动初始化所有内置驱动 (替代 kernel.c 中散落的 init 调用)
void drv_init_all(void);

// root 动态注册一个新驱动 (C 侧入口, 供 krn 模块调用)
// 返回 0 成功, -1 表满
int  drv_register(const char* name, drv_type_t type,
                  int (*init)(void), const char* desc);

// 按名加载(初始化)一个驱动; 返回其 status (1=成功, -1=失败, 0=未找到)
int  drv_load(const char* name);

int        drv_count(void);
driver_t*  drv_get(int i);
driver_t*  drv_find(const char* name);
const char* drv_type_str(drv_type_t t);
const char* drv_status_str(int s);

#endif // DRIVER_H
