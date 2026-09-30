// taskbar.c - FSOS concept-style floating Dock
#include "taskbar.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "theme.h"
#include "theme_api.h"
#include "kb.h"
#include "shell.h"
static void txt(int x,int y,const char*s,uint8_t f,uint8_t b){cjk_ui_text(x,y,s,f,b);}
static void fill_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_fill_round_idx(x0,y0,x1,y1,r,c);}
static void rect_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_round_rect_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_round_rect_idx(x0,y0,x1,y1,r,c);}
void taskbar_init(void){}
void taskbar_get_rect(int*x,int*y,int*w,int*h){*x=0;*y=WM_TASKBAR_Y;*w=VGA_W;*h=WM_TASKBAR_H;}

typedef struct { int action; int kind; } dock_item_t;
static const dock_item_t g_dock[]={{0,ICON_DESKTOP},{6,ICON_SOFTWARE},{ACT_TERMINAL,ICON_TERMINAL},{4,9},{3,ICON_USERMGR},{7,11},{5,ICON_DESKTOP}};
#define NDOCK ((int)(sizeof(g_dock)/sizeof(g_dock[0])))

// 固定项 action -> g_app[] 下标映射表 (-1 = 非应用项: 开始菜单/电源)
static int dock_app_ref(int dock_idx){
    if(dock_idx<0||dock_idx>=NDOCK) return -1;
    int act=g_dock[dock_idx].action;
    if(act==ACT_TERMINAL) return 8;             // 终端 = g_app[8]
    if(act>=0&&act<NAPP) return act;            // 其余固定项 action 即 g_app 下标
    return -1;
}

static int icon_tile(void){return 48;}
static int icon_gap(void){return theme_get_padding(PADDING_MD);}
static int icon_kind(int i){
    switch (i) {
        case 0: return 15;  // 关于
        case 1: return 1;   // 用户
        case 2: return 13;  // 时钟
        case 3: return 14;  // 设置
        case 4: return 9;   // 开发
        case 5: return 6;   // 编辑器
        case 6: return 10;  // 文件管理器
        case 7: return 11;  // 磁盘工具
        case 8: return 4;   // 终端
        default: return ICON_DESKTOP;
    }
}

// 合并后的 Dock 项 (固定项与运行项去重并集)
typedef struct { int action; int kind; int app_ref; int running; int focused; int is_start; } dock_merged_t;

// 构建去重合并列表: 先固定项, 再追加未在固定项中的运行应用, 返回项数
static int dock_build_merged(dock_merged_t* m){
    int n=0, focus=wm_get_focus();
    for(int i=0;i<NDOCK;i++){
        int ar=dock_app_ref(i);
        m[n].action=g_dock[i].action;
        m[n].kind=g_dock[i].kind;
        m[n].app_ref=ar;
        m[n].is_start=(g_dock[i].action==0)?1:0;
        m[n].running=0; m[n].focused=0;
        if(ar>=0&&ar<NAPP&&g_app[ar].open){
            m[n].running=1;
            if(focus==ar&&!g_app[ar].minimized) m[n].focused=1;
        }
        if(m[n].is_start&&g_start_open) m[n].focused=1;
        n++;
    }
    for(int i=0;i<NAPP;i++){
        if(!g_app[i].open) continue;
        int found=0;
        for(int j=0;j<n;j++) if(m[j].app_ref==i){found=1;break;}
        if(found) continue;
        m[n].action=i; m[n].kind=icon_kind(i); m[n].app_ref=i;
        m[n].is_start=0; m[n].running=1;
        m[n].focused=(focus==i&&!g_app[i].minimized)?1:0;
        n++;
    }
    return n;
}

// 按合并项数计算 Dock 几何 (绘制/命中单一来源)
static void dock_geometry_n(int nm,int*x,int*y,int*w,int*h){
    int W=VGA_W;
    int tile=icon_tile(),gap=icon_gap(),pad=theme_get_padding(PADDING_PANEL);
    int content=nm*(tile+gap);
    *w=pad*2+content;
    if(*w<420)*w=420;
    int maxw=W-28; if(*w>maxw)*w=maxw;
    *h=(SCREEN_H>=900)?78:((SCREEN_H>=650)?66:54);
    *x=(W-*w)/2; *y=WM_TASKBAR_Y+(WM_TASKBAR_H-*h)/2;
}

void taskbar_draw(int mx,int my){
    dock_merged_t m[NDOCK+NAPP];
    int nm=dock_build_merged(m);
    int x,y,w,h;dock_geometry_n(nm,&x,&y,&w,&h);
    uint8_t fg=theme_get_color_idx(COLOR_FG),soft=theme_get_color_idx(COLOR_FG_SOFT),accent=theme_get_color_idx(COLOR_ACCENT);
    // macOS 玻璃 Dock: 毛玻璃(Dock 区域背景模糊) + 半透明深色面板 + 亮描边 + 顶部内高光
    int dock_r = theme_get_radius(RADIUS_PANEL_T);
    int blur = theme_get_blur_radius();
    if(blur>0){
        gfx_blur_rgb(x,y,x+w-1,y+h-1,blur);                 // 背景模糊
        gfx_fill_round_rgb_alpha(x,y,x+w,y+h,dock_r, 14,18,30, 150);   // 较低 alpha 让模糊背景透出
    } else {
        gfx_fill_round_rgb_alpha(x,y,x+w,y+h,dock_r, 14,18,30, 176);   // 8bpp/性能不足降级: 半透明纯色
    }
    gfx_round_rect_rgb_aa(x,y,x+w,y+h,dock_r, 104,124,158);
    gfx_fill_rgb_alpha(x+8,y+2,x+w-8,y+3, 255,255,255, 30);
    uint8_t ar,ag,ab; gfx_idx_rgb(accent,&ar,&ag,&ab);
    int tile=icon_tile(),gap=icon_gap(),cur=x+12,iy=y+(h-34)/2;
    for(int i=0;i<nm;i++){
        if(cur+tile>x+w-8) break;
        int hv=(mx>=cur&&mx<cur+tile&&my>=y&&my<y+h);
        if(m[i].focused)      gfx_fill_round_rgb_alpha(cur,y+6,cur+tile,y+h-6,14,ar,ag,ab,70);   // 焦点: 强调蓝高亮
        else if(hv)           gfx_fill_round_rgb_alpha(cur,y+6,cur+tile,y+h-6,14,255,255,255,34); // 悬停: 白色高亮
        if(m[i].is_start){
            int q=10;
            fill_round(cur+17,y+19,cur+17+q,y+29,3,m[i].focused?fg:soft);
            fill_round(cur+31,y+19,cur+31+q,y+29,3,m[i].focused?fg:soft);
            fill_round(cur+17,y+33,cur+17+q,y+43,3,m[i].focused?fg:soft);
            fill_round(cur+31,y+33,cur+31+q,y+43,3,m[i].focused?fg:soft);
        } else {
            draw_icon_big(m[i].kind,cur+8,iy,34);
        }
        // 运行指示: 所有运行中应用底部小圆点 (含最小化)
        if(m[i].running) gfx_disc_aa(cur+tile/2,y+h-9,2,ar,ag,ab);
        cur+=tile+gap;
        if(i==NDOCK-1&&nm>NDOCK){   // 固定项与运行项分隔线
            gfx_line_aa(cur-2,y+14,cur-2,y+h-14,96,116,150);cur+=6;
        }
    }
}


int taskbar_hit_action(int mx, int my){
    dock_merged_t m[NDOCK+NAPP];
    int nm=dock_build_merged(m);
    int x,y,w,h; dock_geometry_n(nm,&x,&y,&w,&h);
    if(mx<x || mx>x+w || my<y || my>y+h) return -1;
    int tile=icon_tile(), gap=icon_gap(), cur=x+12;
    for(int i=0;i<nm;i++){
        if(cur+tile>x+w-8) break;
        if(mx>=cur && mx<cur+tile) return m[i].action;
        cur+=tile+gap;
        if(i==NDOCK-1&&nm>NDOCK) cur+=6;
    }
    return -1;
}
