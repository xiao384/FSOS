// sysconf.h - 系统配置 (root 可修改, 持久化到磁盘)
//
// 配置区位于 layout.h 的 LBA_SYSCONF (内核之后, 镜像末尾之前),
// 与内核镜像区 (LBA 9..约 2538) 物理隔离 -> 修改配置绝不会破坏核心.
//
// 这是"root 可自由修改除核心外一切"的落点:
//   主题/壁纸/主机名/自动启动项 等均可由 root 通过终端或脚本改写并保存.
#ifndef SYSCONF_H
#define SYSCONF_H

#include <stdint.h>
#include "layout.h"   // LBA_SYSCONF, DISK_SECTORS

#define SYSCONF_MAGIC     0x50434653u   // 'PSFC'
#define SYSCONF_SECTORS   4             // 与 layout.h 守卫一致

// 主题: 0=经典蓝 1=暗色 2=墨绿
#define THEME_BLUE   0
#define THEME_DARK   1
#define THEME_GREEN  2

typedef struct {
    uint32_t magic;
    uint8_t  theme;        // THEME_*
    uint8_t  wallpaper;    // COL_* 索引 (壁纸色)
    uint8_t  autostart;    // 预留: 自动启动应用位掩码
    uint8_t  flags;        // 预留
    char     hostname[16];
    uint8_t  dev_app;      // 开发软件选择: 0=内置 DevStudio, 1=宿主 VSCode
    uint8_t  pad[483];     // 凑满 508 字节
} sysconf_t;

// 开发软件选择
#define DEV_APP_DEVSTUDIO 0
#define DEV_APP_VSCODE    1

// 加载: 从磁盘读; 无有效魔数则写入默认配置并保存.
void     sysconf_load(void);
// 当前配置 (加载后有效)
sysconf_t* sysconf(void);
// 持久化到磁盘, 返回 0 成功
int      sysconf_save(void);
// 开发软件选择读写
int      sysconf_dev_app(void);
void     sysconf_set_dev_app(int v);

#endif // SYSCONF_H
