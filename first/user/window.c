// window.c - FSOS modern native-resolution window chrome
#include "window.h"
#include "gfx.h"
#include "cjk.h"
#include "theme_api.h"
#include "vga.h"
#include "safe_callback.h"   // T3.3: 应用回调异常隔离

static int g_maxz=1,g_focus=0,g_drag=-1,g_dox=0,g_doy=0;
int wm_get_focus(void){return g_focus;} void wm_set_focus(int i){g_focus=i;}
int wm_get_maxz(void){return g_maxz;} void wm_set_maxz(int z){g_maxz=z;} int wm_next_z(void){return ++g_maxz;}
int wm_get_drag(void){return g_drag;} void wm_set_drag(int i){g_drag=i;}
int wm_get_dox(void){return g_dox;} int wm_get_doy(void){return g_doy;} void wm_set_drag_offset(int x,int y){g_dox=x;g_doy=y;}

// ---- T3.4: 窗口动画状态机 ----
typedef enum { WM_ANIM_NONE=0, WM_ANIM_MINIMIZE } wm_anim_pending_t;
typedef struct {
    anim_id_t  id;
    int fx,fy,fw,fh, tx,ty,tw,th;
    wm_anim_pending_t pending;
} wm_anim_t;
static wm_anim_t g_wma[NAPP];

static void fill_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_fill_round_idx(x0,y0,x1,y1,r,c);}
static void rect_round(int x0,int y0,int x1,int y1,int r,uint8_t c){uint8_t R,G,B;gfx_idx_rgb(c,&R,&G,&B);if(gfx_is_lfb())gfx_round_rect_rgb_aa(x0,y0,x1,y1,r,R,G,B);else gfx_round_rect_idx(x0,y0,x1,y1,r,c);}
static void txt(int x,int y,const char*s,uint8_t f,uint8_t b){cjk_ui_text(x,y,s,f,b);}
static int btn_size(void){return SCREEN_H>=900?14:(SCREEN_H>=700?13:11);}

// 整数平方根 (无 libc, 用于标题栏圆角裁切)
static int igrad_sqrt(int v){ if(v<=0)return 0; int r=0; for(int b=1<<15;b;b>>=1){int t=r+b; if(t*t<=v)r=t;} return r; }

int wm_win_btn_x(app_t*a,int btn){return a->x+22+btn*26;}

// 标题栏按钮: 交通灯圆点 + 顶部高光 + 暗描边 + 清晰字形
static void draw_win_btn(int bx,int by0,int by1,int bs,uint8_t col){
    fill_round(bx,by0,bx+bs,by1,bs/2,col);
    if(gfx_is_lfb()){
        uint8_t r,g,b; gfx_idx_rgb(col,&r,&g,&b);
        // 暗描边
        uint8_t dr=(uint8_t)(r*60/100),dg=(uint8_t)(g*60/100),db=(uint8_t)(b*60/100);
        gfx_round_rect_rgb_aa(bx,by0,bx+bs,by1,bs/2,dr,dg,db);
        // 顶部高光
        uint8_t hr=(uint8_t)(r+(255-r)*45/100),hg=(uint8_t)(g+(255-g)*45/100),hb=(uint8_t)(b+(255-b)*45/100);
        gfx_fill_rgb(bx+3,by0+2,bx+bs-3,by0+bs/3,hr,hg,hb);
    }
    int cx=bx+bs/2, cy=by0+(by1-by0)/2;
    int d=bs*28/100;
    // 深色字形 (32,36,44)
    if(cy-d<by0+2){ d=cy-by0-2; }
    if(cy+d>by1-2){ d=by1-2-cy; }
    gfx_line_aa(cx-d,cy-d,cx+d,cy+d,32,36,44);
    gfx_line_aa(cx+d,cy-d,cx-d,cy+d,32,36,44);
}

// 标题栏按钮: 最小化 (横线) / 最大化 (方框)
static void draw_win_btn_sym(int bx,int by0,int by1,int bs,uint8_t col,int kind){
    fill_round(bx,by0,bx+bs,by1,bs/2,col);
    if(gfx_is_lfb()){
        uint8_t r,g,b; gfx_idx_rgb(col,&r,&g,&b);
        uint8_t dr=(uint8_t)(r*60/100),dg=(uint8_t)(g*60/100),db=(uint8_t)(b*60/100);
        gfx_round_rect_rgb_aa(bx,by0,bx+bs,by1,bs/2,dr,dg,db);
        uint8_t hr=(uint8_t)(r+(255-r)*45/100),hg=(uint8_t)(g+(255-g)*45/100),hb=(uint8_t)(b+(255-b)*45/100);
        gfx_fill_rgb(bx+3,by0+2,bx+bs-3,by0+bs/3,hr,hg,hb);
    }
    if(kind==1){ // 最小化: 底部横线
        gfx_line_aa(bx+4,by1-4,bx+bs-4,by1-4,32,36,44);
        gfx_line_aa(bx+4,by1-3,bx+bs-4,by1-3,32,36,44);
    } else {     // 最大化: 小圆角方框
        gfx_round_rect_rgb_aa(bx+4,by0+4,bx+bs-4,by1-4,3,32,36,44);
    }
}

void wm_draw_window(app_t*a,int focused){
    int x,y,w,h,th=WM_TITLE_H;
    wm_window_anim_get_geom(a,&x,&y,&w,&h);
    uint8_t panel=theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t title=theme_get_color_idx(COLOR_BG_TITLE);
    uint8_t border=theme_get_color_idx(focused?COLOR_BORDER_FOCUS:COLOR_BORDER);
    uint8_t fg=theme_get_color_idx(COLOR_FG),soft=theme_get_color_idx(COLOR_FG_SOFT);

    // 1) 柔和投影 (聚焦全偏移, 失焦减半 — T3.6 背景窗口弱化)
    if(!a->maximized){
        uint8_t sh=theme_get_color_idx(COLOR_SHADOW);
        int sox=focused?6:3, soy=focused?9:4;
        fill_round(x+sox,y+soy,x+w+sox,y+h+soy,theme_get_radius(RADIUS_WINDOW_T),sh);
    }

    // 2) 窗口主体 (圆角)
    int wr=a->maximized?0:theme_get_radius(RADIUS_WINDOW_T);
    fill_round(x,y,x+w-1,y+h-1,wr,panel);

    // 3) 标题栏渐变 (按圆角裁切顶部两角)
    uint8_t tr,tg,tb; gfx_idx_rgb(title,&tr,&tg,&tb);
    uint8_t lr,lg,lb,br,bg2,bb;
    if(focused){
        lr=(uint8_t)(tr+(255-tr)*40/100); lg=(uint8_t)(tg+(255-tg)*40/100); lb=(uint8_t)(tb+(255-tb)*40/100);
        br=tr; bg2=tg; bb=tb;
    } else {
        uint8_t pr,pg,pb; gfx_idx_rgb(panel,&pr,&pg,&pb);
        lr=(uint8_t)(tr+(pr-tr)*45/100); lg=(uint8_t)(tg+(pg-tg)*45/100); lb=(uint8_t)(tb+(pb-tb)*45/100);
        br=(uint8_t)(tr+(pr-tr)*70/100); bg2=(uint8_t)(tg+(pg-tg)*70/100); bb=(uint8_t)(tb+(pb-tb)*70/100);
        lr=(uint8_t)(lr*85/100); lg=(uint8_t)(lg*85/100); lb=(uint8_t)(lb*85/100);
        br=(uint8_t)(br*85/100); bg2=(uint8_t)(bg2*85/100); bb=(uint8_t)(bb*85/100);
    }
    int rr=wr;
    for(int yy=y; yy<=y+th; yy++){
        int xl=x, xr=x+w-1;
        if(rr>0 && yy<y+rr){
            int dd=rr-(yy-y);
            int lim=rr*rr-dd*dd;
            int dx=lim>0?igrad_sqrt(lim):0;
            int cut=rr-dx;
            xl=x+cut; xr=x+w-1-cut;
        }
        if(xr<xl) continue;
        int t=(yy-y)*256/((th>0)?th:1);
        uint8_t R=(uint8_t)(lr+((int)br-lr)*t/256);
        uint8_t G=(uint8_t)(lg+((int)bg2-lg)*t/256);
        uint8_t B=(uint8_t)(lb+((int)bb-lb)*t/256);
        gfx_fill_rgb(xl,yy,xr,yy,R,G,B);
    }

    // 4) 边框 (聚焦=强调蓝, 失焦=灰)
    if(!a->maximized) rect_round(x,y,x+w-1,y+h-1,theme_get_radius(RADIUS_WINDOW_T),border);
    else rect_round(x,y,x+w-1,y+h-1,0,border);

    // 5) 标题栏按钮
    int bs=btn_size();
    uint8_t danger=theme_get_color_idx(COLOR_DANGER),warn=theme_get_color_idx(COLOR_WARNING),ok=theme_get_color_idx(COLOR_SUCCESS);
    int bx[3]={x+22,x+48,x+74};
    int by0=y+(th-bs)/2, by1=y+(th+bs)/2;
    draw_win_btn(bx[0],by0,by1,bs,danger);          // 关闭 ×
    draw_win_btn_sym(bx[1],by0,by1,bs,warn,1);      // 最小化 —
    draw_win_btn_sym(bx[2],by0,by1,bs,ok,2);        // 最大化 ▢

    // 6) 标题文字 (居中, 聚焦亮/失焦柔)
    int tw=cjk_ui_text_w(a->name),tx=x+(w-tw)/2;
    if(tx<x+94){ tx=x+94; }
    if(tx>w+x-24){ tx=x+94; }
    txt(tx,y+(th-24)/2,a->name,focused?fg:soft,title);

    // 7) 客户区 (保持原 inset API; 经 safe_callback 隔离异常)
    if(a->draw && w>12 && h>th+12) safe_callback_draw(a,x+10,y+th+8,w-20,h-th-16);
}

int wm_round_rect_hit(int mx,int my,int x,int y,int w,int h,int r){
    if(mx<x||my<y||mx>=x+w||my>=y+h)return 0;if(r<=0)return 1;int m=w<h?w:h;if(r>m/4)r=m/4;
    int xr=x+r,yr=y+r;if(mx<xr&&my<yr){int dx=xr-mx,dy=yr-my;if(dx*dx+dy*dy>r*r)return 0;}
    int xr2=x+w-r,yr2=y+r;if(mx>=xr2&&my<yr2){int dx=mx-xr2+1,dy=yr2-my;if(dx*dx+dy*dy>r*r)return 0;}
    int xb=x+r,yb=y+h-r;if(mx<xb&&my>=yb){int dx=xb-mx,dy=my-yb+1;if(dx*dx+dy*dy>r*r)return 0;}
    int xb2=x+w-r,yb2=y+h-r;if(mx>=xb2&&my>=yb2){int dx=mx-xb2+1,dy=my-yb2+1;if(dx*dx+dy*dy>r*r)return 0;}
    return 1;
}
int wm_hit_test(app_t*a,int mx,int my,int*what){int x=a->x,y=a->y,w=a->w,h=a->h;if(!wm_round_rect_hit(mx,my,x,y,w,h,a->maximized?0:theme_get_radius(RADIUS_WINDOW_T))){*what=HT_NONE;return 0;}int th=WM_TITLE_H,bs=btn_size();if(my<y+th){for(int i=0;i<3;i++){int bx=wm_win_btn_x(a,i);if(mx>=bx-4&&mx<=bx+bs+4){*what=(i==0?HT_CLOSE:(i==1?HT_MIN:HT_MAX));return 1;}}*what=HT_TITLE;return 1;}*what=HT_CLIENT;return 1;}
// ==================== T3.4: 窗口动画状态机实现 ====================
static int wma_idx(app_t*a){for(int i=0;i<NAPP;i++)if(&g_app[i]==a)return i;return -1;}

anim_id_t wm_window_anim_begin(app_t*a,anim_type_t type,int to_x,int to_y,int to_w,int to_h){
    int idx=wma_idx(a); if(idx<0) return ANIM_INVALID;
    int dur=theme_get_anim_duration(ANIM_WINDOW_T); if(dur<=0) return ANIM_INVALID;
    if(g_wma[idx].id!=ANIM_INVALID){anim_cancel(g_wma[idx].id);g_wma[idx].pending=WM_ANIM_NONE;}
    anim_id_t id=anim_create(type,dur); if(id==ANIM_INVALID) return ANIM_INVALID;
    wm_anim_t*an=&g_wma[idx]; an->id=id; an->tx=to_x;an->ty=to_y;an->tw=to_w;an->th=to_h;
    an->pending=(type==ANIM_WINDOW_MINIMIZE)?WM_ANIM_MINIMIZE:WM_ANIM_NONE;
    if(type==ANIM_WINDOW_OPEN){int cx=to_x+to_w/2,cy=to_y+to_h/2;an->fw=to_w*9/10;an->fh=to_h*9/10;an->fx=cx-an->fw/2;an->fy=cy-an->fh/2;}
    else{an->fx=a->x;an->fy=a->y;an->fw=a->w;an->fh=a->h;}
    return id;
}

void wm_window_anim_get_geom(app_t*a,int*x,int*y,int*w,int*h){
    *x=a->x;*y=a->y;*w=a->w;*h=a->h;
    int idx=wma_idx(a); if(idx<0) return;
    if(g_wma[idx].id==ANIM_INVALID||!anim_is_active(g_wma[idx].id)) return;
    int p=anim_get_progress(g_wma[idx].id);
    wm_anim_t*an=&g_wma[idx];
    *x=an->fx+(an->tx-an->fx)*p/1000;
    *y=an->fy+(an->ty-an->fy)*p/1000;
    *w=an->fw+(an->tw-an->fw)*p/1000;
    *h=an->fh+(an->th-an->fh)*p/1000;
}

int wm_window_anim_active(app_t*a){
    int idx=wma_idx(a); if(idx<0) return 0;
    return g_wma[idx].id!=ANIM_INVALID && anim_is_active(g_wma[idx].id);
}

void wm_window_anim_tick(void){
    for(int i=0;i<NAPP;i++){
        if(g_wma[i].id==ANIM_INVALID) continue;
        if(anim_is_active(g_wma[i].id)) continue;
        g_wma[i].id=ANIM_INVALID;
        if(g_wma[i].pending==WM_ANIM_MINIMIZE) g_app[i].minimized=1;
        g_wma[i].pending=WM_ANIM_NONE;
    }
}
