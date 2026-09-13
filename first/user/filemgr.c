// filemgr.c - FSOS 文件管理器 (支持文件夹/子目录)
//
// 功能: 浏览磁盘文件与文件夹、显示文件名/大小/类型、打开到编辑器、新建文件/文件夹、
//       重命名/删除、进入子目录与返回上级、顶部显示路径面包屑与磁盘/FS 容量信息。
// 与 wm 的 app_t 回调契约一致 (draw/key/on_mouse 签名同其它窗口应用)。
#include "filemgr.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "kb.h"
#include "filesys.h"
#include "editor.h"
#include "wm.h"
#include "ata.h"
#include "layout.h"
#include "theme.h"
#include <stdint.h>

#define FM_HDR_H   19     // Finder 工具栏（容纳 16px 汉字）
#define FM_INFO_H  32     // 两行: 磁盘容量+路径 / FS 用量
#define FM_ROW_H   16     // 列表行高 (容纳 16px 汉字)
#define FM_FOOT_H  18     // 状态/提示行
#define FM_SIDE_W  (VGA_W * 72 / 320)  // hires: 按屏幕宽度缩放侧边栏

static char g_fm_names[FS_MAX_FILES][FS_NAME_SZ];
static int  g_fm_size[FS_MAX_FILES];
static uint8_t g_fm_type[FS_MAX_FILES];
static int  g_fm_n = 0;
static int  g_fm_sel = 0;
static int  g_fm_top = 0;
static char g_fm_msg[40];
static int  g_fm_mode = 0;        // 0 常规, 1 输入(改名/新建文件/新建文件夹)
static int  g_fm_op   = 0;        // 输入模式: 0 改名, 1 新建文件, 2 新建文件夹
static char g_fm_input[FS_NAME_SZ];
static int  g_fm_confirm = 0;     // 删除二次确认
static int  g_fm_delidx = -1;
static int  g_fm_list0 = 0;       // 列表区屏幕起始 Y (draw 时记录, on_mouse 用)
static int  g_fm_listH = 0;       // 列表区像素高
static int  g_fm_listX = 0, g_fm_listW = 0;
static int  g_fm_x = 0, g_fm_y = 0, g_fm_w = 0;

// 当前目录导航状态
static uint32_t g_fm_dir = LBA_FS_DIR;             // 当前目录块 LBA
static uint32_t g_fm_stack[8];                    // 祖先目录 LBA 栈
static char     g_fm_path[8][FS_NAME_SZ];         // 祖先目录名栈 (面包屑)
static int      g_fm_depth = 0;                   // 当前层级深度

// 右键上下文菜单
static int   g_fm_ctx = 0;            // 菜单是否打开
static int   g_fm_ctx_x = 0, g_fm_ctx_y = 0;  // 菜单屏幕坐标
static int   g_fm_ctx_n = 0;         // 菜单项数
static int   g_fm_ctx_act[8];        // 各菜单项动作: 1打开/进入 2改名 3删除 4新建文件 5刷新 6新建文件夹 7上级
static int   g_fm_ctx_file = -1;     // 右键所在文件索引 (-1=空白处)

// ---- 极小字符串助手 (内核 -ffreestanding, 无 libc) ----
static int fm_strlen(const char* s){ int n=0; while(s&&s[n]) n++; return n; }
static void set_msg(const char* s){ char* p=g_fm_msg; while(*s)*p++=*s++; *p=0; }
static char* app_str(char* p, const char* s){ while(*s)*p++=*s++; return p; }
static char* app_uint(char* p, unsigned u){
    if(u==0){ *p++='0'; return p; }
    char t[16]; int i=0;
    while(u){ t[i++]=(char)('0'+(u%10)); u/=10; }
    while(i) *p++=t[--i];
    return p;
}
// 写入 "X.XXGB" (tot 为总扇区数); 返回新指针
static char* app_gb(char* p, uint64_t tot){
    uint64_t bytes = tot * 512u;                 // 总字节
    uint64_t gx = bytes * 100u / 1000000000u;   // GB*100
    p = app_uint(p, (unsigned)(gx/100));
    *p++='.';
    unsigned d = (unsigned)(gx%100);
    if(d<10) *p++='0';
    p = app_uint(p, d);
    p = app_str(p, "GB");
    return p;
}

static int fm_isdir(int idx){ return (idx>=0 && idx<g_fm_n && g_fm_type[idx]==FS_TYPE_DIR && !(g_fm_names[idx][0]=='.'&&g_fm_names[idx][1]=='.')); }
static int fm_isup(int idx){  return (idx>=0 && idx<g_fm_n && g_fm_names[idx][0]=='.'&&g_fm_names[idx][1]=='.'); }
static void fm_refresh(void);

static void fm_enter(const char* name){
    if(g_fm_depth >= 8){ set_msg("目录层级过深"); return; }
    uint32_t sub = fs_subdir_lba(g_fm_dir, name);
    if(sub == 0) return;
    g_fm_stack[g_fm_depth] = g_fm_dir;
    int i=0; while(name[i] && i<FS_NAME_SZ-1){ g_fm_path[g_fm_depth][i]=name[i]; i++; }
    g_fm_path[g_fm_depth][i]=0;
    g_fm_depth++;
    g_fm_dir = sub;
    g_fm_sel = 0; g_fm_top = 0;
    fm_refresh();
}
static void fm_up(void){
    if(g_fm_depth <= 0){ set_msg("已在根目录"); return; }
    g_fm_depth--;
    g_fm_dir = g_fm_stack[g_fm_depth];
    g_fm_sel = 0; g_fm_top = 0;
    fm_refresh();
}

static void fm_refresh(void){
    g_fm_n = 0;
    if(g_fm_depth > 0){
        g_fm_names[0][0]='.'; g_fm_names[0][1]='.'; g_fm_names[0][2]=0;
        g_fm_type[0] = FS_TYPE_DIR; g_fm_size[0] = 0;
        g_fm_n = 1;
    }
    int m = fs_list_in(g_fm_dir, g_fm_names + g_fm_n, g_fm_type + g_fm_n, FS_MAX_FILES - g_fm_n);
    g_fm_n += m;
    for(int i=0;i<g_fm_n;i++){
        if(fm_isdir(i)) g_fm_size[i] = 0;
        else if(!fm_isup(i)) g_fm_size[i] = fs_size_in(g_fm_dir, g_fm_names[i]);
    }
    if(g_fm_sel>=g_fm_n) g_fm_sel=g_fm_n-1;
    if(g_fm_sel<0) g_fm_sel=0;
    g_fm_confirm=0; g_fm_delidx=-1;
    g_fm_ctx=0;
}

void filemgr_open(void){
    g_fm_dir = LBA_FS_DIR; g_fm_depth = 0;
    g_fm_msg[0]=0; g_fm_mode=0; g_fm_sel=0; g_fm_top=0;
    fm_refresh();
}

static void fm_path_str(char* out){
    char* p=out; const char* r="根"; while(*r)*p++=*r++;
    for(int i=0;i<g_fm_depth;i++){ *p++='/'; const char* s=g_fm_path[i]; while(*s)*p++=*s++; }
    *p=0;
}

void filemgr_draw(int x,int y,int w,int h){
    // Finder 工具栏：应用窗口外层已有标题栏，这里只提供当前位置与常用操作。
    g_fm_x=x; g_fm_y=y; g_fm_w=w;
    gfx_fill_idx(x, y, x+w-1, y+FM_HDR_H-1, COL_TASKBAR);
    cjk_text(x+5, y+1, "文件", COL_WHITE, COL_TASKBAR);
    cjk_text(x+42, y+1, "浏览", COL_LGRAY, COL_TASKBAR);
    char cnt[16]; char* p=cnt;
    p=app_str(p," "); p=app_uint(p,(unsigned)g_fm_n); p=app_str(p,"/"); p=app_uint(p,(unsigned)FS_MAX_FILES);
    int cw=cjk_text_w(cnt); cjk_text(x+w-4-cw, y+1, cnt, COL_LGRAY, COL_TASKBAR);

    // 左侧边栏：只保留根目录与上一级两个低门槛入口，键盘操作仍完全可用。
    int side_y=y+FM_HDR_H, side_bottom=y+h-FM_FOOT_H;
    gfx_fill_idx(x, side_y, x+FM_SIDE_W-1, side_bottom-1, COL_TITLEBG);
    cjk_text(x+6, side_y+3, "位置", COL_LGRAY, COL_TITLEBG);
    uint8_t root_bg=(g_fm_depth==0)?COL_ACCENT_SOFT:COL_TITLEBG;
    gfx_fill_round_idx(x+3, side_y+18, x+FM_SIDE_W-4, side_y+35, RADIUS_CTRL, root_bg);
    cjk_text(x+8, side_y+19, "根目录", COL_WHITE, root_bg);
    if(g_fm_depth>0){
        gfx_fill_round_idx(x+3, side_y+39, x+FM_SIDE_W-4, side_y+56, RADIUS_CTRL, COL_TITLEBG);
        cjk_text(x+8, side_y+40, "上级目录", COL_LCYAN, COL_TITLEBG);
    }
    cjk_text(x+6, side_y+70, "操作", COL_LGRAY, COL_TITLEBG);
    cjk_text(x+8, side_y+86, "N 新建", COL_WHITE, COL_TITLEBG);
    cjk_text(x+8, side_y+102, "B 文件夹", COL_WHITE, COL_TITLEBG);

    int mx=x+FM_SIDE_W, mw=w-FM_SIDE_W;

    // 信息区 (两行: 磁盘容量+路径 / FS 用量)
    int iy=y+FM_HDR_H;
    gfx_fill_idx(mx, iy, mx+mw-1, iy+FM_INFO_H-1, COL_PANEL);
    int ue=0, us=0; fs_stats_total(&ue,&us);
    int used_kb = us/2;                              // 已用扇区 -> KB
    int free_kb = (FS_DATA_SECS - us)/2;            // FS 数据区剩余 KB
    uint64_t total = ata_total_sectors();
    // 第一行: "磁盘X.XXGB 路径 " + 面包屑 (面包屑超宽则省略)
    char l1[40]; char* p1=l1;
    p1=app_str(p1,"磁盘"); p1=app_gb(p1, total); p1=app_str(p1," 路径 "); *p1=0;
    cjk_text(mx+4, iy+1, l1, COL_WHITE, COL_PANEL);
    int px0 = mx+4+cjk_text_w(l1);
    char pbuf[64]; fm_path_str(pbuf);
    cjk_text_ellipsis(px0, iy+1, pbuf, mx+mw-4-px0, COL_LCYAN, COL_PANEL);
    // 第二行: FS 用量
    char l2[64]; char* p2=l2;
    p2=app_str(p2,"已用 "); p2=app_uint(p2,(unsigned)ue); p2=app_str(p2,"项 ");
    p2=app_uint(p2,(unsigned)used_kb); p2=app_str(p2,"KB  剩 ");
    p2=app_uint(p2,(unsigned)free_kb); p2=app_str(p2,"KB");
    *p2=0;
    cjk_text(mx+4, iy+17, l2, COL_WHITE, COL_PANEL);

    // 列表区
    int ly0=y+FM_HDR_H+FM_INFO_H;
    int listH=h-FM_HDR_H-FM_INFO_H-FM_FOOT_H;
    if(listH<0) listH=0;
    int rows=listH/FM_ROW_H;
    if(g_fm_sel<g_fm_top) g_fm_top=g_fm_sel;
    if(g_fm_sel>=g_fm_top+rows) g_fm_top=g_fm_sel-rows+1;
    if(g_fm_top<0) g_fm_top=0;
    g_fm_list0=ly0; g_fm_listH=listH; g_fm_listX=mx; g_fm_listW=mw;

    gfx_fill_idx(mx, ly0, mx+mw-1, ly0+listH-1, COL_WHITE);
    for(int r=0;r<rows;r++){
        int idx=g_fm_top+r;
        if(idx>=g_fm_n) break;
        int ry=ly0+r*FM_ROW_H;
        int sel=(idx==g_fm_sel);
        uint8_t bg = sel?COL_ACCENT_SOFT:COL_WHITE;
        uint8_t fg = sel?COL_ACCENT:COL_BLACK;
        if(sel) gfx_fill_round_idx(mx+2, ry, mx+mw-3, ry+FM_ROW_H-1, RADIUS_CTRL, COL_ACCENT_SOFT);
        // 名称 (文件夹追加 "/" 标记)
        char disp[FS_NAME_SZ+2];
        int di=0; while(g_fm_names[idx][di] && di<FS_NAME_SZ-1){ disp[di]=g_fm_names[idx][di]; di++; }
        disp[di]=0;
        if(fm_isdir(idx)){ disp[di++]='/'; disp[di]=0; }
        cjk_text(mx+5, ry, disp, fm_isdir(idx)?COL_LBLUE:fg, bg);
        // 大小/类型
        char sbuf[16]; char* sp=sbuf; *sp++=' ';
        if(fm_isup(idx)){ sp=app_str(sp,"上级"); }
        else if(fm_isdir(idx)){ sp=app_str(sp,"目录"); }
        else {
            int sz=g_fm_size[idx];
            if(sz<0) sp=app_str(sp,"?");
            else if(sz>=1024){ sp=app_uint(sp,(unsigned)(sz/1024)); *sp++='K'; }
            else sp=app_uint(sp,(unsigned)sz);
        }
        *sp=0;
        int sw=cjk_text_w(sbuf);
        cjk_text(mx+mw-4-sw, ry, sbuf, fg, bg);
    }
    if(g_fm_n==0) cjk_text(mx+5, ly0+2, "（空）按 N 新建", COL_LGRAY, COL_WHITE);

    // 输入模式: 覆盖信息行显示输入框
    if(g_fm_mode==1){
        gfx_fill_idx(mx, iy, mx+mw-1, iy+FM_INFO_H-1, COL_BLACK);
        const char* prompt;
        if(g_fm_op==0) prompt="改名为: ";
        else if(g_fm_op==1) prompt="新建文件名: ";
        else prompt="新建文件夹名: ";
        cjk_text(mx+4, iy+8, prompt, COL_WHITE, COL_BLACK);
        int px=mx+4+cjk_text_w(prompt);
        cjk_text(px, iy+8, g_fm_input, COL_WHITE, COL_BLACK);
        int il=cjk_text_w(g_fm_input);
        gfx_fill_idx(px+il+1, iy+8, px+il+6, iy+23, COL_WHITE); // 闪烁光标块
    }

    // 右键上下文菜单 (绘制于窗口客户区内, 坐标已夹取)
    if(g_fm_ctx){
        int mw=116, mh=g_fm_ctx_n*18+4;
        int px=g_fm_ctx_x, py=g_fm_ctx_y;
        if(px+mw > x+w-2) px = x+w-2-mw;
        if(px < x+2) px = x+2;
        if(py+mh > y+h-2) py = y+h-2-mh;
        if(py < y+2) py = y+2;
        gfx_fill_idx(px, py, px+mw, py+mh, COL_LGRAY);
        gfx_rect_idx(px+1, py+1, px+mw-1, py+mh-1, COL_DGRAY);
        for(int i=0;i<g_fm_ctx_n;i++){
            int iy2=py+2+i*18;
            int act=g_fm_ctx_act[i];
            const char* lbl;
            switch(act){
                case 1: lbl = fm_isup(g_fm_ctx_file)?"进入上级":(fm_isdir(g_fm_ctx_file)?"进入文件夹":"打开"); break;
                case 2: lbl="改名"; break;
                case 3: lbl = fm_isdir(g_fm_ctx_file)?"删除文件夹":"删除"; break;
                case 4: lbl="新建文件"; break;
                case 5: lbl="刷新"; break;
                case 6: lbl="新建文件夹"; break;
                case 7: lbl="上级目录"; break;
                default: lbl="";
            }
            cjk_text(px+6, iy2+1, lbl, COL_BLACK, COL_LGRAY);
        }
    }

    // 状态/提示行
    int fy=y+h-FM_FOOT_H;
    gfx_fill_idx(x, fy, x+w-1, y+h-1, COL_TASKBAR);
    const char* foot = g_fm_msg[0]? g_fm_msg : "Enter 打开 · Del 删除 · F2 改名";
    cjk_text(x+5, fy+1, foot, COL_LGRAY, COL_TASKBAR);
}

int filemgr_key(int k){
    if(g_fm_mode==1){
        if(k==27){ g_fm_mode=0; g_fm_msg[0]=0; return 1; }                 // ESC 取消
        if(k=='\r'){
            g_fm_input[FS_NAME_SZ-1]=0;
            if(g_fm_input[0]){
                if(g_fm_op==0){                                          // 改名
                    fs_rename_in(g_fm_dir, g_fm_names[g_fm_sel], g_fm_input);
                    set_msg("已改名");
                } else if(g_fm_op==1){                                   // 新建文件
                    fs_write_in(g_fm_dir, g_fm_input, "");
                    wm_launch_app(5); editor_open_file_in(g_fm_input, g_fm_dir);
                } else {                                                 // 新建文件夹
                    int rc=fs_mkdir(g_fm_dir, g_fm_input);
                    set_msg(rc==0?"已新建文件夹":(rc==-1?"重名":"数据区满"));
                }
                fm_refresh();
            }
            g_fm_mode=0; g_fm_msg[0]=0; return 1;
        }
        if(k=='\b'){ int l=fm_strlen(g_fm_input); if(l>0) g_fm_input[l-1]=0; return 1; }
        if(k>=32 && k<=126 && fm_strlen(g_fm_input)<FS_NAME_SZ-1){
            int l=fm_strlen(g_fm_input); g_fm_input[l]=(char)k; g_fm_input[l+1]=0; return 1;
        }
        return 1;
    }
    // 常规模式
    if(k=='u'||k=='U'||k=='\b'){ g_fm_confirm=0; fm_up(); return 1; }     // 上级目录
    if(k==KEY_UP || k=='w'||k=='W'){ g_fm_confirm=0; if(g_fm_sel>0)g_fm_sel--; }
    else if(k==KEY_DOWN || k=='s'||k=='S'){ g_fm_confirm=0; if(g_fm_sel<g_fm_n-1)g_fm_sel++; }
    else if(k==KEY_HOME){ g_fm_confirm=0; g_fm_sel=0; }
    else if(k==KEY_END){ g_fm_confirm=0; g_fm_sel=g_fm_n-1; }
    else if(k=='\r' || k=='o'||k=='O'){
        if(g_fm_n>0 && g_fm_sel>=0 && g_fm_sel<g_fm_n){
            if(fm_isup(g_fm_sel)) fm_up();
            else if(fm_isdir(g_fm_sel)) fm_enter(g_fm_names[g_fm_sel]);
            else { wm_launch_app(5); editor_open_file_in(g_fm_names[g_fm_sel], g_fm_dir); }
        }
    }
    else if(k==KEY_DEL || k=='d'||k=='D'){
        if(g_fm_n>0 && !fm_isup(g_fm_sel)){
            if(!g_fm_confirm){ g_fm_confirm=1; g_fm_delidx=g_fm_sel; set_msg("再次按 Del 删除"); }
            else if(g_fm_sel==g_fm_delidx){
                if(fm_isdir(g_fm_sel)){ int rc=fs_rmdir(g_fm_dir, g_fm_names[g_fm_sel]); set_msg(rc==0?"已删除":(rc==-2?"文件夹非空":"删除失败")); }
                else { fs_remove_in(g_fm_dir, g_fm_names[g_fm_sel]); set_msg("已删除"); }
                fm_refresh();
            } else { g_fm_confirm=0; }
        }
    }
    else if(k=='n'||k=='N'){ g_fm_mode=1; g_fm_op=1; g_fm_input[0]=0; }
    else if(k=='b'||k=='B'){ g_fm_mode=1; g_fm_op=2; g_fm_input[0]=0; }
    else if(k==KEY_F2 || k=='m'||k=='M'){
        if(g_fm_n>0 && !fm_isup(g_fm_sel)){
            g_fm_mode=1; g_fm_op=0;
            int i=0; while(g_fm_names[g_fm_sel][i] && i<FS_NAME_SZ-1){ g_fm_input[i]=g_fm_names[g_fm_sel][i]; i++; }
            g_fm_input[i]=0;
        }
    }
    else if(k=='r'||k=='R'||k==KEY_F5 || k=='f'||k=='F'){ fm_refresh(); g_fm_msg[0]=0; }
    return 1;
}

static void filemgr_ctx_exec(int act){
    if(act==1){                                         // 打开/进入
        if(g_fm_ctx_file>=0 && g_fm_ctx_file<g_fm_n){
            if(fm_isup(g_fm_ctx_file)) fm_up();
            else if(fm_isdir(g_fm_ctx_file)) fm_enter(g_fm_names[g_fm_ctx_file]);
            else { wm_launch_app(5); editor_open_file_in(g_fm_names[g_fm_ctx_file], g_fm_dir); }
        }
    } else if(act==2){                                  // 改名
        if(g_fm_ctx_file>=0 && g_fm_ctx_file<g_fm_n && !fm_isup(g_fm_ctx_file)){
            g_fm_mode=1; g_fm_op=0;
            int i=0; while(g_fm_names[g_fm_ctx_file][i] && i<FS_NAME_SZ-1){ g_fm_input[i]=g_fm_names[g_fm_ctx_file][i]; i++; }
            g_fm_input[i]=0;
        }
    } else if(act==3){                                  // 删除/删除文件夹
        if(g_fm_ctx_file>=0 && g_fm_ctx_file<g_fm_n && !fm_isup(g_fm_ctx_file)){
            if(fm_isdir(g_fm_ctx_file)){ int rc=fs_rmdir(g_fm_dir, g_fm_names[g_fm_ctx_file]); set_msg(rc==0?"已删除":(rc==-2?"文件夹非空":"删除失败")); }
            else { fs_remove_in(g_fm_dir, g_fm_names[g_fm_ctx_file]); set_msg("已删除"); }
            fm_refresh();
        }
    } else if(act==4){                                  // 新建文件
        g_fm_mode=1; g_fm_op=1; g_fm_input[0]=0;
    } else if(act==5){                                  // 刷新
        fm_refresh(); g_fm_msg[0]=0;
    } else if(act==6){                                  // 新建文件夹
        g_fm_mode=1; g_fm_op=2; g_fm_input[0]=0;
    } else if(act==7){                                  // 上级目录
        fm_up();
    }
}

int filemgr_on_rmouse(int mx,int my){
    int row=(my-g_fm_list0)/FM_ROW_H + g_fm_top;
    int on_file=(mx>=g_fm_listX && mx<g_fm_listX+g_fm_listW &&
                 my>=g_fm_list0 && my<g_fm_list0+g_fm_listH && row>=0 && row<g_fm_n);
    g_fm_ctx_n=0;
    if(on_file){
        g_fm_sel=row; g_fm_ctx_file=row;
        if(fm_isup(row)){
            g_fm_ctx_act[g_fm_ctx_n++]=1;  // 进入上级
        } else if(fm_isdir(row)){
            g_fm_ctx_act[g_fm_ctx_n++]=1;  // 进入文件夹
            g_fm_ctx_act[g_fm_ctx_n++]=2;  // 改名
            g_fm_ctx_act[g_fm_ctx_n++]=3;  // 删除文件夹
            g_fm_ctx_act[g_fm_ctx_n++]=6;  // 新建文件夹
        } else {
            g_fm_ctx_act[g_fm_ctx_n++]=1;  // 打开
            g_fm_ctx_act[g_fm_ctx_n++]=2;  // 改名
            g_fm_ctx_act[g_fm_ctx_n++]=3;  // 删除
            g_fm_ctx_act[g_fm_ctx_n++]=4;  // 新建文件
        }
        g_fm_ctx_act[g_fm_ctx_n++]=5;      // 刷新
    } else {
        g_fm_ctx_file=-1;
        if(g_fm_depth>0) g_fm_ctx_act[g_fm_ctx_n++]=7;  // 上级目录
        g_fm_ctx_act[g_fm_ctx_n++]=4;  // 新建文件
        g_fm_ctx_act[g_fm_ctx_n++]=6;  // 新建文件夹
        g_fm_ctx_act[g_fm_ctx_n++]=5;  // 刷新
    }
    g_fm_ctx_x=mx; g_fm_ctx_y=my; g_fm_ctx=1;
    return 1;
}

void filemgr_close_ctx(void){ g_fm_ctx=0; }

int filemgr_on_mouse(int mx,int my,int ldown){
    if(g_fm_ctx){                                   // 右键菜单打开时, 本点击优先消费
        int mw=116, mh=g_fm_ctx_n*18+4;
        int mx0=g_fm_ctx_x, my0=g_fm_ctx_y;
        if(mx>=mx0 && mx<=mx0+mw && my>=my0 && my<=my0+mh){
            int it=(my-my0-2)/18;
            if(it>=0 && it<g_fm_ctx_n){ filemgr_ctx_exec(g_fm_ctx_act[it]); g_fm_ctx=0; return 1; }
        }
        g_fm_ctx=0; return 1;                       // 点其它处关闭菜单
    }
    if(ldown!=1) return 0;
    // Finder 侧边栏：根目录 / 上一级是明确的鼠标入口。
    int side_y=g_fm_y+FM_HDR_H;
    if(mx>=g_fm_x && mx<g_fm_x+FM_SIDE_W && my>=side_y && my<g_fm_y+SCREEN_H){
        if(my>=side_y+18 && my<side_y+36){
            g_fm_dir=LBA_FS_DIR; g_fm_depth=0; g_fm_sel=0; g_fm_top=0; fm_refresh();
            return 1;
        }
        if(g_fm_depth>0 && my>=side_y+39 && my<side_y+57){ fm_up(); return 1; }
        return 1;
    }
    if(mx<g_fm_listX || mx>=g_fm_listX+g_fm_listW) return 0;
    if(my<g_fm_list0 || my>=g_fm_list0+g_fm_listH) return 0;
    int row=(my-g_fm_list0)/FM_ROW_H + g_fm_top;
    if(row<0 || row>=g_fm_n) return 0;
    if(row==g_fm_sel){                       // 二次点击 -> 打开/进入
        if(fm_isup(row)) fm_up();
        else if(fm_isdir(row)) fm_enter(g_fm_names[row]);
        else { wm_launch_app(5); editor_open_file_in(g_fm_names[row], g_fm_dir); }
    } else {
        g_fm_sel=row; g_fm_confirm=0;
    }
    return 1;
}
