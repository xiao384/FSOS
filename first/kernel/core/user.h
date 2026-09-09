// user.h - 用户账户管理 (内存 + 磁盘持久化)
#ifndef USER_H
#define USER_H

#include <stdint.h>

#define USER_MAX      16
#define USER_NAME_SZ  15   // 含 NUL, 最多 14 字符
#define USER_PASS_SZ  15

#define ROLE_NORMAL 0
#define ROLE_ADMIN  1
#define ROLE_ROOT   2   // 最高权限: 可修改除内核核心运行以外的所有配置

typedef struct {
    uint8_t magic;    // 0x55 = 有效
    uint8_t role;     // ROLE_NORMAL / ROLE_ADMIN
    char name[USER_NAME_SZ];
    char pass[USER_PASS_SZ];
} UserRec;

// 从磁盘加载; 首次运行自动创建默认用户 (admin/admin, guest/guest)
void user_init(void);
// 内存数据写回磁盘; 返回 0 成功
int  user_save(void);

int  user_count(void);
const UserRec* user_get(int idx);
int  user_find(const char* name);

int  user_add(const char* name, const char* pass, uint8_t role);
int  user_remove(int idx);
int  user_rename(int idx, const char* name);
int  user_setpass(int idx, const char* pass);
int  user_setrole(int idx, uint8_t role);

// 登录验证: 密码匹配返回 0, 否则 -1
int  user_verify(int idx, const char* pass);

/* 当前登录会话 (由 app 登录流程设置, 供终端/BT 跟随系统账户) */
int  user_session(void);
void user_session_set(int index);
const char* user_session_name(void);

/* 查找第一个管理员用户; 失败返回 -1 */
int  user_find_admin(void);
/* 验证管理员密码 (任一管理员密码匹配即 root 提权通过) */
int  user_verify_root(const char* pass);

#endif // USER_H
