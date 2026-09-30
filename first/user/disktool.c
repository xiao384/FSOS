// disktool.c - FSOS 磁盘工具
//
// 显示: 磁盘型号/总容量(真实扇区数), 文件系统(FS)用量, 以及本系统按 layout.h 划分的
// 逻辑"分区/区域" (引导/内核/用户库/文件系统/系统配置/解释器模块)。
// 提供"格式化文件系统"功能: 清空用户文件区 (根目录块清零, 数据区不擦除)。
// 注: 本系统不使用 MBR/GPT, 而是固定 LBA 布局, 故"分区"即上述逻辑区域。
#include "disktool.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "kb.h"
#include "filesys.h"
#include "ata.h"
#include "layout.h"
#include <stdint.h>

#define DT_HDR_H 19
#define DT_FOOT_H 18

typedef struct { const char* name; uint32_t lba; uint32_t secs; } region_t;

// 逻辑区域表 (取自 layout.h, 单一权威定义)
static const region_t g_reg[] = {
    { "引导扇区",   LBA_BOOT,        1 },
    { "二级引导",   LBA_LOADER,      8 },
    { "内核镜像",   LBA_KERNEL,      LBA_USER_SB - LBA_KERNEL },
    { "用户库",     LBA_USER_SB,     2 },
    { "文件系统目录", LBA_FS_DIR,     FS_DIR_SECS },
    { "文件系统数据", LBA_FS_DATA,    FS_DATA_SECS },
    { "系统配置",   LBA_SYSCONF,     4 },
    { "CINT 模块",  LBA_MOD_CINT,    MOD_REGION_SECTORS },
    { "JVM 模块",   LBA_MOD_JVM,     MOD_REGION_SECTORS },
};
#define NREG (int)(sizeof(g_reg)/sizeof(g_reg[0]))

static char g_dt_msg[48];
static int  g_dt_armed = 0;       // 格式化待确认标志

static int dt_strlen(const char* s){ int n=0; while(s&&s[n]) n++; return n; }
static char* dt_str(char* p, const char* s){ while(*s)*p++=*s++; return p; }
static char* dt_u(char* p, uint64_t u){
    if(u==0){ *p++='0'; return p; }
    char t[24]; int i=0;
    while(u){ t[i++]=(char)('0'+(u%10)); u/=10; }
    while(i) *p++=t[--i];
    return p;
}
// 写 "X.XXGB"
static char* dt_gb(char* p, uint64_t tot){
    uint64_t bytes = tot * 512u;
    uint64_t gx = bytes * 100u / 1000000000u;
    p = dt_u(p, gx/100); *p++='.';
    unsigned d=(unsigned)(gx%100); if(d<10)*p++='0'; p=dt_u(p,d);
    p=dt_str(p,"GB"); return p;
}

void disktool_open(void){ g_dt_armed=0; g_dt_msg[0]=0; }

void disktool_draw(int x,int y,int w,int h){
    gfx_fill_idx(x, y, x+w-1, y+h-1, COL_WHITE);
    // 标题
    gfx_fill_idx(x, y, x+w-1, y+DT_HDR_H-1, COL_ACCENT);
    cjk_text(x+4, y+1, "磁盘工具", COL_WHITE, COL_ACCENT);

    int cy = y+DT_HDR_H+3;
    const char* mdl = ata_model();
    char line[96]; char* p;

    // 磁盘型号 + 总容量
    p=line; p=dt_str(p,"磁盘 ");
    int mc=0; while(mdl[mc]&&mc<16){ *p++=mdl[mc++]; }
    p=dt_str(p," 总"); p=dt_gb(p, ata_total_sectors());
    *p=0; cjk_text(x+4, cy, line, COL_BLACK, COL_WHITE); cy+=18;

    // 文件系统用量
    int ue=0, us=0; fs_stats_total(&ue,&us);
    int used_kb = us/2;
    int free_kb = (FS_DATA_SECS - us)/2;
    p=line; p=dt_str(p,"FS 已用 "); p=dt_u(p,ue); p=dt_str(p," 项 ");
    p=dt_u(p,used_kb); p=dt_str(p,"KB 剩 "); p=dt_u(p,free_kb); p=dt_str(p,"KB");
    *p=0; cjk_text(x+4, cy, line, COL_BLACK, COL_WHITE); cy+=18;

    // 逻辑分区/区域表 (名称按像素宽度对齐到 88px 即 8 个汉字列)
    p=line; p=dt_str(p,"逻辑分区 (LBA 布局):"); *p=0;
    cjk_text(x+4, cy, line, COL_ACCENT, COL_WHITE); cy+=18;
    for(int i=0;i<NREG;i++){
        if(cy > y+h-DT_FOOT_H-16) break;
        p=line;
        p=dt_str(p, g_reg[i].name);
        int nw = cjk_text_w(g_reg[i].name);              // 像素宽
        for(int s=0; s<(88-nw)/8; s++) *p++=' ';         // 补空格对齐 LBA 列
        p=dt_str(p,"LBA "); p=dt_u(p, g_reg[i].lba);
        p=dt_str(p," +"); p=dt_u(p, g_reg[i].secs); p=dt_str(p," 扇区");
        *p=0;
        uint8_t fg = (i%2)? COL_DGRAY : COL_BLACK;
        cjk_text(x+8, cy, line, fg, COL_WHITE);
        cy+=16;
    }

    // 提示/状态行
    int fy=y+h-DT_FOOT_H;
    gfx_fill_idx(x, fy, x+w-1, y+h-1, COL_PANEL);
    if(g_dt_armed){
        cjk_text(x+3, fy+1, "按 Y 确认格式化文件系统, 其它键取消", COL_YELLOW, COL_PANEL);
    } else {
        const char* foot = g_dt_msg[0]? g_dt_msg : "F 格式化文件系统  R 刷新";
        cjk_text(x+3, fy+1, foot, COL_WHITE, COL_PANEL);
    }
}

int disktool_key(int k){
    if(g_dt_armed){
        if(k=='y'||k=='Y'){
            fs_format();
            g_dt_armed=0;
            char* p=g_dt_msg; p=dt_str(p,"已格式化文件系统"); *p=0;
        } else {
            g_dt_armed=0; g_dt_msg[0]=0;
        }
        return 1;
    }
    if(k=='f'||k=='F'){ g_dt_armed=1; g_dt_msg[0]=0; return 1; }
    if(k=='r'||k=='R'){ g_dt_msg[0]=0; return 1; }
    return 1;
}

int disktool_on_mouse(int mx,int my,int ldown){ (void)mx; (void)my; (void)ldown; return 0; }
