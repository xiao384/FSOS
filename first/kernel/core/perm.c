// perm.c - 权限 / 有效身份层实现
#include "perm.h"

// 默认 ROOT: 终端历史即为最高权限, 故未显式设置时视为 root.
static uint8_t g_role = ROLE_ROOT;

void perm_set_role(uint8_t role) { g_role = role; }
uint8_t perm_role(void)          { return g_role; }
int  perm_is_root(void)          { return g_role == ROLE_ROOT; }
int  perm_can_modify(void) {
    return g_role == ROLE_ROOT || g_role == ROLE_ADMIN;
}
