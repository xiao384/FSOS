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
#include "theme_api.h"   // gui_framework Phase 9: Theme API 集成点
#include "shell.h"       // modern icon renderer
#include <stdint.h>

#define FM_HDR_H   48     // Finder 工具栏（容纳 16px 汉字）
#define FM_INFO_H  76     // 两行: 磁盘容量+路径 / FS 用量
#define FM_ROW_H   28     // 列表行高 (容纳 16px 汉字)
#define FM_FOOT_H  30     // 状态/提示行
#define FM_SIDE_W  (VGA_W >= 1400 ? 190 : (VGA_W >= 900 ? 165 : 120))

static void txt(int x,int y,const char*s,uint8_t f,uint8_t b){cjk_ui_text(x,y,s,f,b);}
static void txt_ellipsis(int x,int y,const char*s,int w,uint8_t f,uint8_t b){cjk_ui_text_ellipsis(x,y,s,w,f,b);}
static void fill_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_fill_round_idx(x0,y0,x1,y1,r,c);}
static void rect_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_round_rect_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_round_rect_idx(x0,y0,x1,y1,r,c);}

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
    g_fm_x=x;g_fm_y=y;g_fm_w=w;
    uint8_t panel=theme_get_color_idx(COLOR_BG_PANEL), title=theme_get_color_idx(COLOR_BG_TITLE), field=theme_get_color_idx(COLOR_FIELD), bg=theme_get_color_idx(COLOR_BG), fg=theme_get_color_idx(COLOR_FG), soft=theme_get_color_idx(COLOR_FG_SOFT), accent=theme_get_color_idx(COLOR_ACCENT), hover=theme_get_color_idx(COLOR_HOVER), border=theme_get_color_idx(COLOR_BORDER);
    gfx_fill_idx(x,y,x+w-1,y+h-1,panel);
    // toolbar
    gfx_fill_idx(x,y,x+w-1,y+FM_HDR_H-1,title);txt(x+18,y+15,"文件",fg,title);txt(x+64,y+15,"主目录",soft,title);
    fill_round(x+w-260,y+9,x+w-18,y+39,14,field);txt(x+w-240,y+16,"搜索文件...",soft,field);
    // sidebar
    int side_y=y+FM_HDR_H, side_bottom=y+h-FM_FOOT_H;
    gfx_fill_idx(x,side_y,x+FM_SIDE_W-1,side_bottom-1,theme_get_color_idx(COLOR_BG_MENU));
    const char* nav[8]={"主页","桌面","文档","图片","音乐","视频","下载","此电脑"};
    for(int i=0;i<8;i++){int yy=side_y+16+i*34;int sel=(i==0);if(sel)fill_round(x+10,yy-5,x+FM_SIDE_W-10,yy+25,10,hover);txt(x+26,yy,nav[i],sel?fg:soft,sel?hover:theme_get_color_idx(COLOR_BG_MENU));}
    gfx_line_aa(x+FM_SIDE_W,side_y,x+FM_SIDE_W,side_bottom-1,45,68,92);
    int mx=x+FM_SIDE_W,mw=w-FM_SIDE_W;
    // quick access cards
    txt(mx+24,side_y+20,"快速访问",fg,panel);
    const char* quick[6]={"桌面","文档","图片","音乐","视频","下载"}; const int kinds[6]={10,10,6,7,9,5};
    int cardw=(mw-60)/3;
    for(int i=0;i<6;i++){int cx=mx+20+(i%3)*(cardw+10),cy=side_y+42+(i/3)*78;fill_round(cx,cy,cx+cardw,cy+64,14,field);draw_icon_big(kinds[i],cx+12,cy+9,42);txt_ellipsis(cx+64,cy+14,quick[i],cardw-76,fg,field);txt(cx+64,cy+37,"此电脑",soft,field);}
    // storage
    int sy=side_y+42+156;txt(mx+24,sy,"存储设备",fg,panel);fill_round(mx+20,sy+30,mx+mw-20,sy+98,16,field);draw_icon_big(11,mx+34,sy+43,42);txt(mx+92,sy+42,"系统盘 (C:)",fg,field);fill_round(mx+92,sy+70,mx+mw-46,sy+78,4,hover);fill_round(mx+92,sy+70,mx+mw-180,sy+78,4,accent);txt(mx+92,sy+82,"36.4 GB / 100 GB",soft,field);
    // list of real FS entries
    int ly0=sy+118;int listH=h-FM_HDR_H-FM_FOOT_H-(ly0-y);if(listH<80)listH=80;g_fm_list0=ly0;g_fm_listH=listH;g_fm_listX=mx;g_fm_listW=mw;int rows=listH/FM_ROW_H;if(g_fm_sel<g_fm_top)g_fm_top=g_fm_sel;if(g_fm_sel>=g_fm_top+rows)g_fm_top=g_fm_sel-rows+1;if(g_fm_top<0)g_fm_top=0;
    for(int r=0;r<rows;r++){int idx=g_fm_top+r;if(idx>=g_fm_n)break;int ry=ly0+r*FM_ROW_H;if(idx==g_fm_sel)fill_round(mx+18,ry,mx+mw-18,ry+FM_ROW_H-3,10,hover);txt(mx+32,ry+6,g_fm_names[idx],idx==g_fm_sel?fg:soft,idx==g_fm_sel?hover:panel);char sbuf[16];char*sp=sbuf;if(fm_isdir(idx))sp=app_str(sp,"文件夹");else sp=app_str(sp,"文件");*sp=0;txt(mx+mw-94,ry+6,sbuf,soft,idx==g_fm_sel?hover:panel);}
    // input mode
    if(g_fm_mode==1){int bx=mx+22,by=sy+112;fill_round(bx,by,mx+mw-22,by+58,14,field);const char*prompt=(g_fm_op==0?"改名为：":(g_fm_op==1?"新建文件：":"新建文件夹："));txt(bx+16,by+10,prompt,soft,field);int pw2=cjk_ui_text_w(prompt);txt(bx+16+pw2,by+10,g_fm_input,fg,field);}
    // context menu
    if(g_fm_ctx){int mmw=150,mmh=g_fm_ctx_n*26+8,px=g_fm_ctx_x,py=g_fm_ctx_y;if(px+mmw>x+w)px=x+w-mmw;if(py+mmh>y+h)py=y+h-mmh;fill_round(px,py,px+mmw,py+mmh,14,theme_get_color_idx(COLOR_BG_MENU));rect_round(px,py,px+mmw,py+mmh,14,border);for(int i=0;i<g_fm_ctx_n;i++){int iy2=py+4+i*26;int hv=0;if(hv)fill_round(px+5,iy2,px+mmw-5,iy2+24,9,hover);const char*lbl="";switch(g_fm_ctx_act[i]){case 1:lbl=fm_isdir(g_fm_ctx_file)?"打开":"打开";break;case 2:lbl="重命名";break;case 3:lbl="删除";break;case 4:lbl="新建文件";break;case 5:lbl="刷新";break;case 6:lbl="新建文件夹";break;case 7:lbl="上级目录";break;}txt(px+16,iy2+5,lbl,fg,hv?hover:theme_get_color_idx(COLOR_BG_MENU));}}
    // footer
    int fy=y+h-FM_FOOT_H;gfx_fill_idx(x,fy,x+w-1,y+h-1,title);txt(x+18,fy+7,g_fm_msg[0]?g_fm_msg:"Enter 打开 · N 新建 · F2 重命名 · Del 删除",soft,title);
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
