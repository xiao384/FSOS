// user.c - 用户账户管理
// 磁盘布局见 layout.h (唯一权威定义)
//   超级块 { magic[4]="USR1", count, reserved }
//   UserRec[16] (512 字节)
//
// 重要: 必须存放在【内核镜像之后】的空闲扇区。旧值 LBA 64/65 落在内核镜像内部,
// 于是 user_save() 会把用户库写进内核代码区, 导致"首次启动成功后镜像被写坏,
// 之后每次启动都崩溃"的假性间歇故障。位置现由 layout.h 统一给出。
#include "user.h"
#include "ata.h"
#include "layout.h"

#define USER_SB_LBA  LBA_USER_SB
#define USER_REC_LBA LBA_USER_REC
#define SB_MAGIC0 'U'
#define SB_MAGIC1 'S'
#define SB_MAGIC2 'R'
#define SB_MAGIC3 '1'

typedef struct {
    char magic[4];
    uint8_t count;
    uint8_t reserved[507];
} SuperBlock;

static UserRec g_users[USER_MAX];
static int g_count = 0;

static int str_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void wipe_users(void) {
    for (int i = 0; i < USER_MAX; i++) {
        g_users[i].magic = 0;
        g_users[i].role = ROLE_NORMAL;
        g_users[i].name[0] = '\0';
        g_users[i].pass[0] = '\0';
    }
}

static void init_default_users(void) {
    wipe_users();
    g_count = 0;
    user_add("admin", "admin", ROLE_ADMIN);
    user_add("guest", "guest", ROLE_NORMAL);
}

void user_init(void) {
    SuperBlock sb;
    wipe_users();
    if (ata_read_sectors(USER_SB_LBA, 1, &sb) == 0 &&
        sb.magic[0] == SB_MAGIC0 && sb.magic[1] == SB_MAGIC1 &&
        sb.magic[2] == SB_MAGIC2 && sb.magic[3] == SB_MAGIC3 &&
        sb.count <= USER_MAX) {
        g_count = sb.count;
        if (ata_read_sectors(USER_REC_LBA, 1, g_users) != 0) {
            init_default_users();
            return;
        }
        // 统计实际有效记录
        g_count = 0;
        for (int i = 0; i < USER_MAX; i++) {
            if (g_users[i].magic == 0x55) g_count++;
        }
    } else {
        init_default_users();
        user_save();
    }
}

int user_save(void) {
    SuperBlock sb;
    sb.magic[0] = SB_MAGIC0; sb.magic[1] = SB_MAGIC1;
    sb.magic[2] = SB_MAGIC2; sb.magic[3] = SB_MAGIC3;
    sb.count = (uint8_t)g_count;
    for (int i = 0; i < 507; i++) sb.reserved[i] = 0;
    if (ata_write_sectors(USER_SB_LBA, 1, &sb) != 0) return -1;
    if (ata_write_sectors(USER_REC_LBA, 1, g_users) != 0) return -1;
    return 0;
}

int user_count(void) { return g_count; }

const UserRec* user_get(int idx) {
    if (idx < 0 || idx >= g_count) return 0;
    return &g_users[idx];
}

int user_find(const char* name) {
    for (int i = 0; i < g_count; i++) {
        if (g_users[i].magic == 0x55 && str_eq(g_users[i].name, name))
            return i;
    }
    return -1;
}

int user_add(const char* name, const char* pass, uint8_t role) {
    if (g_count >= USER_MAX) return -1;
    if (!name[0]) return -1;
    if (user_find(name) >= 0) return -2;   // 重名
    UserRec* u = &g_users[g_count];
    u->magic = 0x55;
    u->role = role;
    str_copy(u->name, name, USER_NAME_SZ);
    str_copy(u->pass, pass, USER_PASS_SZ);
    g_count++;
    return 0;
}

int user_remove(int idx) {
    if (idx < 0 || idx >= g_count) return -1;
    // 不允许删除最后一个管理员
    if (g_users[idx].role == ROLE_ADMIN) {
        int admins = 0;
        for (int i = 0; i < g_count; i++)
            if (g_users[i].magic == 0x55 && g_users[i].role == ROLE_ADMIN) admins++;
        if (admins <= 1) return -2;
    }
    for (int i = idx; i < g_count - 1; i++) g_users[i] = g_users[i + 1];
    g_count--;
    g_users[g_count].magic = 0;
    return 0;
}

int user_rename(int idx, const char* name) {
    if (idx < 0 || idx >= g_count) return -1;
    if (!name[0]) return -1;
    for (int i = 0; i < g_count; i++) {
        if (i != idx && g_users[i].magic == 0x55 && str_eq(g_users[i].name, name))
            return -2;
    }
    str_copy(g_users[idx].name, name, USER_NAME_SZ);
    return 0;
}

int user_setpass(int idx, const char* pass) {
    if (idx < 0 || idx >= g_count) return -1;
    str_copy(g_users[idx].pass, pass, USER_PASS_SZ);
    return 0;
}

int user_setrole(int idx, uint8_t role) {
    if (idx < 0 || idx >= g_count) return -1;
    g_users[idx].role = role;
    return 0;
}

int user_verify(int idx, const char* pass) {
    if (idx < 0 || idx >= g_count) return -1;
    return str_eq(g_users[idx].pass, pass) ? 0 : -1;
}
