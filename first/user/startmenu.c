// startmenu.c - FSOS concept-style launcher
#include "startmenu.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "theme.h"
#include "theme_api.h"
#include "shell.h"
#include "animation.h"
static anim_id_t g_open_anim=ANIM_INVALID; static int g_menu_target=0;
static void txt(int x,int y,const char*s,uint8_t f,uint8_t b){cjk_ui_text(x,y,s,f,b);}
static void fill_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_fill_round_idx(x0,y0,x1,y1,r,c);}
static void rect_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_round_rect_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_round_rect_idx(x0,y0,x1,y1,r,c);}
void startmenu_set_open(int open){g_menu_target=open?1:0;if(g_open_anim!=ANIM_INVALID)anim_cancel(g_open_anim);g_open_anim=anim_create(open?ANIM_STARTMENU_OPEN:ANIM_WINDOW_CLOSE,theme_get_anim_duration(ANIM_MENU_T));}
static int menu_w(void){int w=(VGA_W>=1500?620:(VGA_W>=1000?560:460));if(w>VGA_W-24)w=VGA_W-24;return w;}
static int menu_h(void){int h=(SCREEN_H>=900?500:((SCREEN_H>=700)?450:SCREEN_H-44));return h<300?300:h;}
int startmenu_width(void){return menu_w();}int startmenu_height(void){return menu_h();}
int startmenu_x0(void){return 22;}int startmenu_y0(void){return WM_TASKBAR_Y-menu_h()-14;}
static int anim_y(void){int y=startmenu_y0();if(g_open_anim!=ANIM_INVALID&&anim_is_active(g_open_anim)){int p=anim_get_progress(g_open_anim);y+=18-(18*p/1000);}return y;}
void startmenu_item_rect(int i,int*rx,int*ry,int*rw,int*rh){int x=startmenu_x0(),y=anim_y(),side=154;int cols=3,gap=10,top=116,pad=16;int right_w=menu_w()-side-pad*2;int tile=(right_w-gap*(cols-1))/cols;*rx=x+side+pad+(i%cols)*(tile+gap);*ry=y+top+(i/cols)*74;*rw=tile;*rh=62;}
int startmenu_footer_hit(int mx,int my){
    int x=startmenu_x0(),y=anim_y(),w=menu_w(),h=menu_h(),fy=y+h-54;
    if(my<fy+6||my>fy+42)return 0;
    if(mx>=x+w-138 && mx<x+w-74)return ACT_LOGOFF;
    if(mx>=x+w-66 && mx<x+w-18)return ACT_POWEROFF;
    return 0;
}
void startmenu_draw(int mx,int my){
    int x=startmenu_x0(),y=anim_y(),w=menu_w(),h=menu_h();
    uint8_t panel=theme_get_color_idx(COLOR_BG_MENU),field=theme_get_color_idx(COLOR_FIELD),hover=theme_get_color_idx(COLOR_HOVER),fg=theme_get_color_idx(COLOR_FG),soft=theme_get_color_idx(COLOR_FG_SOFT),accent=theme_get_color_idx(COLOR_ACCENT),border=theme_get_color_idx(COLOR_BORDER);
    fill_round(x,y,x+w,y+h,24,panel);rect_round(x,y,x+w,y+h,24,border);
    fill_round(x+18,y+18,x+w-18,y+60,14,field);txt(x+40,y+29,"搜索应用、文件、设置...",soft,field);
    // category rail
    int rail=x+18;fill_round(rail,y+76,rail+136,y+h-70,16,theme_get_color_idx(COLOR_BG_PANEL));
    const char* cats[6]={"最近使用","所有应用","办公","开发","娱乐","设置"};for(int i=0;i<6;i++){int yy=y+92+i*40;int sel=(i==0);if(sel)fill_round(rail+8,yy-4,rail+128,yy+26,10,hover);txt(rail+20,yy,cats[i],sel?fg:soft,sel?hover:theme_get_color_idx(COLOR_BG_PANEL));}
    // app grid
    txt(x+172,y+82,"最近使用",fg,panel);
    int count=g_nstart-1; if(count>12)count=12;
    for(int i=0;i<count;i++){int rx,ry,rw,rh;startmenu_item_rect(i,&rx,&ry,&rw,&rh);int hv=mx>=rx&&mx<rx+rw&&my>=ry&&my<ry+rh;if(hv)fill_round(rx,ry,rx+rw,ry+rh,14,hover);draw_icon_big(g_start[i].kind,rx+12,ry+6,46);txt(rx+68,ry+15,g_start[i].label,fg,hv?hover:panel);txt(rx+68,ry+39,(i==4?"开发工具":"应用"),soft,hv?hover:panel);}
    // footer
    int fy=y+h-54;gfx_line_aa(x+18,fy,x+w-18,fy,56,70,92);txt(x+32,fy+20,"Administrator",fg,panel);fill_round(x+w-138,fy+6,x+w-74,fy+42,12,hover);txt(x+w-120,fy+14,"注销",fg,hover);fill_round(x+w-66,fy+6,x+w-18,fy+42,12,theme_get_color_idx(COLOR_DANGER));txt(x+w-54,fy+14,"关机",fg,theme_get_color_idx(COLOR_DANGER));
}
