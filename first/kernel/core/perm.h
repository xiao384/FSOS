// perm.h - 权限 / 有效身份层
//
// 设计目标 (对应需求 "root 可自由修改除系统核心运行以外的一切"):
//   - 系统维护一个"当前有效身份" (effective role).
//   - 终端以 ROOT 身份运行 (历史即最高权限), 登录 GUI 后切换为所登录用户的角色.
//   - perm_can_modify() 决定当前身份能否修改"非核心"配置 (主题/命令/脚本等).
//   - "核心运行" (内核镜像 LBA 9..约 2538) 在任何身份下都不可写:
//       没有代码路径会向该区域写盘, 且构建时 layout 守卫禁止数据落进内核区.
//       因此"核心"由架构保证不可修改, 与权限无关.
#ifndef PERM_H
#define PERM_H

#include "user.h"   // ROLE_*

// 设置当前有效身份 (登录时由 app, 终端入口设为 ROOT)
void perm_set_role(uint8_t role);
// 取当前有效身份
uint8_t perm_role(void);
// 是否为 ROOT (最高)
int  perm_is_root(void);
// 能否修改系统(非核心): ROOT 或 ADMIN 均可
int  perm_can_modify(void);

#endif // PERM_H
