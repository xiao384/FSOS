// sysconf.c - 系统配置持久化实现
#include "sysconf.h"
#include "ata.h"

static sysconf_t g_cfg;
static int g_loaded = 0;

void sysconf_load(void) {
    if (g_loaded) return;
    if (ata_read_sectors(LBA_SYSCONF, 1, &g_cfg) == 0 &&
        g_cfg.magic == SYSCONF_MAGIC) {
        g_loaded = 1;
        return;
    }
    // 无有效配置 -> 默认
    for (int i = 0; i < (int)sizeof(g_cfg); i++) ((uint8_t*)&g_cfg)[i] = 0;
    g_cfg.magic     = SYSCONF_MAGIC;
    g_cfg.theme     = THEME_BLUE;
    g_cfg.wallpaper = 1;            // COL_BLUE
    g_cfg.hostname[0] = 'p'; g_cfg.hostname[1] = 'x';
    g_cfg.hostname[2] = 's'; g_cfg.hostname[3] = '\0';
    sysconf_save();
    g_loaded = 1;
}

sysconf_t* sysconf(void) { return &g_cfg; }

int sysconf_dev_app(void) { return g_cfg.dev_app ? 1 : 0; }

void sysconf_set_dev_app(int v) {
    g_cfg.dev_app = (uint8_t)(v ? 1 : 0);
    sysconf_save();
}

int sysconf_save(void) {
    return ata_write_sectors(LBA_SYSCONF, 1, &g_cfg);
}
