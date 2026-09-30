// shell.c - FSOS 现代桌面外壳
// 第一性原则：Shell 只负责桌面级空间与交互，不再把 320x200 当成 GUI 的设计基准。
#include "shell.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "icon.h"
#include "theme.h"
#include "theme_api.h"
#include "taskbar.h"
#include "wm.h"
#include "io.h"
#include "aurora_wallpaper_full.h"
#include "power.h"

static uint32_t g_star_seed = 0x13579BDFu;

static void txt(int x,int y,const char* s,uint8_t f,uint8_t b){ cjk_ui_text(x,y,s,f,b); }
static void txt_ellipsis(int x,int y,const char* s,int w,uint8_t f,uint8_t b){ cjk_ui_text_ellipsis(x,y,s,w,f,b); }
static void txt_ellipsis_over(int x,int y,const char* s,int w,uint8_t f){ cjk_ui_text_ellipsis_over(x,y,s,w,f); }
static void fill_round(int x0,int y0,int x1,int y1,int r,uint8_t c){
    uint8_t cr,cg,cb; gfx_idx_rgb(c,&cr,&cg,&cb);
    if(gfx_is_lfb()) gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,cr,cg,cb);
    else gfx_fill_round_idx(x0,y0,x1,y1,r,c);
}
static void rect_round(int x0,int y0,int x1,int y1,int r,uint8_t c){
    uint8_t cr,cg,cb; gfx_idx_rgb(c,&cr,&cg,&cb);
    if(gfx_is_lfb()) gfx_round_rect_rgb_aa(x0,y0,x1,y1,r,cr,cg,cb);
    else gfx_round_rect_idx(x0,y0,x1,y1,r,c);
}

static uint32_t rnd(void){
    g_star_seed ^= g_star_seed << 13; g_star_seed ^= g_star_seed >> 17; g_star_seed ^= g_star_seed << 5; return g_star_seed;
}

// ==================== 壁纸 ====================
void desktop_draw_wallpaper(void){
    int W=VGA_W, H=SCREEN_H;
    if(gfx_is_lfb()) {
        // 缩放(cover) + 上/下遮罩只算一次并缓存, 之后每帧仅内存拷贝。
        // (原本每帧重跑 30 万像素缩放且逐像素含 64 位除法, 是帧率低的主因。)
        int th = (H>120)?58:0, bh = (H>120)?30:0;
        if(!gfx_desktop_wallpaper_present(aurora_wallpaper_full,AURORA_FULL_W,AURORA_FULL_H,
                                          th,bh,84,104, 4,8,18)) {
            // 非 32bpp LFB: 退回旧的直接 blit 路径
            gfx_blit_rgb565_cover(aurora_wallpaper_full,AURORA_FULL_W,AURORA_FULL_H,W,H);
        }
    } else {
        // 兼容 8bpp：继续使用轻量程序化背景。
        uint8_t ctop=COL_WALL_F, cb=COL_WALL_D;
        gfx_gradient_v_idx(0,0,W-1,H*58/100,ctop,COL_WALL_B);
        gfx_gradient_v_idx(0,H*58/100,W-1,H-1,COL_WALL_C,cb);
    }
}

// ==================== 顶部状态栏 (macOS 菜单栏风格) ====================
int desktop_topbar_h(void){ return (SCREEN_H>=900)?44:38; }
// 右侧状态簇 (wifi/音量/电池/日期时间) 左边界, 同时也是控制中心热区左边界
int desktop_topbar_cluster_x(void){
    int aw=(gfx_font_scale()>=2)?16:8;
    int cluster=18*aw+112;
    if(VGA_W<720) cluster=14*aw+96;
    int x=VGA_W-12-cluster;
    if(x<150)x=150;
    return x;
}
static void draw_wifi(int cx,int cy,uint8_t c){
    uint8_t r,g,b; gfx_idx_rgb(c,&r,&g,&b);
    if(gfx_is_lfb()){
        gfx_line_aa(cx-9,cy-2,cx,cy+6,r,g,b); gfx_line_aa(cx,cy+6,cx+9,cy-2,r,g,b);
        gfx_line_aa(cx-5,cy+2,cx,cy+6,r,g,b); gfx_line_aa(cx,cy+6,cx+5,cy+2,r,g,b);
        gfx_line_aa(cx-2,cy+6,cx,cy+8,r,g,b); gfx_line_aa(cx,cy+8,cx+2,cy+6,r,g,b);
    }
}
static void draw_battery(int x,int y,uint8_t c){
    rect_round(x,y,x+22,y+12,3,c); fill_round(x+24,y+4,x+26,y+8,1,c); fill_round(x+3,y+3,x+16,y+9,2,c);
}
static void draw_speaker(int x,int y,uint8_t c){
    uint8_t r,g,b; gfx_idx_rgb(c,&r,&g,&b);
    if(!gfx_is_lfb())return;
    gfx_fill_rgb(x,y+4,x+2,y+8,r,g,b);
    gfx_line_aa(x+2,y+6,x+6,y+2,r,g,b);
    gfx_line_aa(x+2,y+6,x+6,y+10,r,g,b);
    gfx_line_aa(x+6,y+2,x+6,y+10,r,g,b);
    gfx_line_aa(x+9,y+4,x+9,y+8,r,g,b);
}
void desktop_draw_topbar(void){
    int H=desktop_topbar_h(); int W=VGA_W;
    int cw=(gfx_font_scale()>=2)?24:16;
    uint8_t fg=theme_get_color_idx(COLOR_FG), accent=theme_get_color_idx(COLOR_ACCENT), fgt=theme_get_color_idx(COLOR_FG_TITLE);
    // macOS 玻璃顶栏: 半透明深色 + 底部 1px 微光分隔
    gfx_fill_rgb_alpha(0,0,W-1,H-1, 12,16,26, 200);
    gfx_fill_rgb(0,H-1,W-1,H-1, 48,60,84);
    // 左: FSOS 徽标方块 + 名称 (透明背景文字)
    fill_round(14,8,38,H-8,9,accent);
    txt(20,13,"FS",fgt,accent);
    cjk_ui_text_over(48,(H-cw)/2+1,"FSOS",fg);
    // 右: 日期 时间 / 电池 / 音量 / wifi (整簇同时是控制中心热区)
    rtc_update();
    char full[24]; int p=0;
    if(rtc_ok){
        full[p++]='2';full[p++]='0';full[p++]=(char)('0'+(rtc_y/10)%10);full[p++]=(char)('0'+rtc_y%10);full[p++]='/';
        full[p++]=(char)('0'+rtc_mo/10);full[p++]=(char)('0'+rtc_mo%10);full[p++]='/';
        full[p++]=(char)('0'+rtc_d/10);full[p++]=(char)('0'+rtc_d%10);full[p++]=' ';
        full[p++]=(char)('0'+rtc_h/10);full[p++]=(char)('0'+rtc_h%10);full[p++]=':';
        full[p++]=(char)('0'+rtc_m/10);full[p++]=(char)('0'+rtc_m%10);
    } else { full[p++]='-';full[p++]='-';full[p++]=':';full[p++]='-';full[p++]='-'; }
    full[p]=0;
    int ty=(H-cw)/2+1;
    int tw=cjk_ui_text_w(full);
    cjk_ui_text_over(W-16-tw,ty,full,fg);
    int right=W-16-tw-24;
    int battery_x=right-26; if(battery_x>150){draw_battery(battery_x,(H-12)/2,fg);right=battery_x-20;}
    int sp_x=right-12; if(sp_x>140){draw_speaker(sp_x,(H-12)/2,fg);right=sp_x-20;}
    if(right>130) draw_wifi(right-8,H/2+2,fg);

    // 控制中心展开/收起箭头 (位于状态簇最左, 与热区一致)
    int cx = desktop_topbar_cluster_x();
    int open = sidebar_is_open();
    uint8_t cc = open ? accent : theme_get_color_idx(COLOR_FG_SOFT);
    uint8_t cr, cg, cb; gfx_idx_rgb(cc, &cr, &cg, &cb);
    int chx = cx - 16, chy = H / 2;
    if (open) {            // 向上 (收起)
        gfx_line_aa(chx - 5, chy - 3, chx, chy + 3, cr, cg, cb);
        gfx_line_aa(chx, chy + 3, chx + 5, chy - 3, cr, cg, cb);
    } else {               // 向下 (展开)
        gfx_line_aa(chx - 5, chy + 3, chx, chy - 3, cr, cg, cb);
        gfx_line_aa(chx, chy - 3, chx + 5, chy + 3, cr, cg, cb);
    }
}

// ==================== 桌面图标 ====================
static int icon_x(int i){(void)i; return 42;}
static int icon_y(int i){ return 76+i*118; }
void desktop_draw_icons(int mx,int my){
    uint8_t fg=theme_get_color_idx(COLOR_FG), soft=theme_get_color_idx(COLOR_FG_SOFT), accent=theme_get_color_idx(COLOR_ACCENT);
    int maxy=WM_TASKBAR_Y-18;
    for(int i=0;i<g_nicon;i++){
        int x=icon_x(i), y=icon_y(i); if(y+96>maxy) break;
        int hover=(mx>=18&&mx<=142&&my>=y-10&&my<=y+94), sel=(i==g_sel_icon);
        if(hover){ gfx_fill_round_rgb_alpha(x-16,y-10,x+94,y+90,18,255,255,255,28); }
        if(sel){
            gfx_fill_round_rgb_alpha(x-16,y-10,x+94,y+90,18,255,255,255,44);
            rect_round(x-16,y-10,x+94,y+90,18,accent);
        }
        draw_icon_big(g_icons[i].kind,x,y,54);
        int tw=cjk_ui_text_w(g_icons[i].label); if(tw>100)tw=100;
        // 壁纸上的标签: 深色投影 + 白字 (无背景格)
        txt_ellipsis_over(x+27-tw/2+2,y+68,g_icons[i].label,100,soft);
        txt_ellipsis_over(x+27-tw/2,y+66,g_icons[i].label,100,fg);
    }
}
int desktop_icon_hit(int mx,int my){
    for(int i=0;i<g_nicon;i++){int y=icon_y(i);if(mx>=18&&mx<=142&&my>=y-10&&my<=y+94)return i;} return -1;
}

// ==================== 新手提示 ====================
void desktop_draw_tip(void){
    if(!g_tip_visible||SCREEN_H<420)return;
    int w=(VGA_W>=1200)?470:400; int h=62; int x=(VGA_W-w)/2; int y=62;
    uint8_t field=theme_get_color_idx(COLOR_BG_MENU),fg=theme_get_color_idx(COLOR_FG),soft=theme_get_color_idx(COLOR_FG_SOFT),border=theme_get_color_idx(COLOR_BORDER);
    fill_round(x,y,x+w,y+h,16,field); rect_round(x,y,x+w,y+h,16,border);
    fill_round(x+16,y+14,x+48,y+46,12,theme_get_color_idx(COLOR_ACCENT_SOFT));
    txt(x+26,y+20,"FS",fg,theme_get_color_idx(COLOR_ACCENT_SOFT));
    txt(x+64,y+10,"欢迎使用 FSOS！",fg,field);
    txt_ellipsis(x+64,y+33,"你可以在开始菜单中找到更多应用程序。",w-86,soft,field);
}

// ==================== 帮助 ====================
void desktop_draw_help_overlay(void){
    int w=680,h=520; if(w>VGA_W-64)w=VGA_W-64; if(h>WM_TASKBAR_Y-64)h=WM_TASKBAR_Y-64; int x=(VGA_W-w)/2,y=(WM_TASKBAR_Y-h)/2;
    uint8_t panel=theme_get_color_idx(COLOR_BG_MENU),fg=theme_get_color_idx(COLOR_FG),soft=theme_get_color_idx(COLOR_FG_SOFT),border=theme_get_color_idx(COLOR_BORDER);
    fill_round(x,y,x+w,y+h,24,panel); rect_round(x,y,x+w,y+h,24,border);
    txt(x+28,y+24,"键盘与操作",fg,panel); txt(x+28,y+58,"FSOS Desktop",soft,panel);
    int colw=(w-80)/2, rows=(g_nhk+1)/2;
    for(int i=0;i<g_nhk;i++){int col=i/rows,row=i%rows;txt_ellipsis(x+28+col*colw,y+96+row*38,g_hotkeys[i].label,colw-20,fg,panel);}    
    txt(x+28,y+h-42,"F1 关闭此面板",soft,panel);
}

// ==================== 电源 ====================
void desktop_poweroff(void){
    // 优先走 ACPI FADT/_S5_ 真正关机；失败时返回到桌面，而不是把 CPU 停住。
    (void)poweroff_system();
}
void desktop_reboot(void){
    // 优先 ACPI reset；无 ACPI 时再用 8042 reset。
    if (reboot_system() != 0) {
        outb(0x64,0xFE);
    }
}


// ==================== 右键菜单 ====================
int desktop_ctx_hit(int mx,int my){
    int w=300,h=g_nctx*40+20,x=g_ctx_x,y=g_ctx_y; if(x+w>VGA_W)x=VGA_W-w;if(y+h>SCREEN_H)y=SCREEN_H-h;
    if(mx<x||mx>x+w||my<y||my>y+h) return -1;
    int i=(my-(y+10))/40;
    return(i>=0&&i<g_nctx)?i:-1;
}
void desktop_ctx_execute(int idx){int act=g_ctx[idx].act;g_ctx_open=0;g_force_redraw=1;if(act==-99)return;if(act==-98){desktop_poweroff();return;}if(act==-97){desktop_reboot();return;}wm_launch_app(act);}
void desktop_draw_context_menu(int mx,int my){
    int w=300,h=g_nctx*40+20,x=g_ctx_x,y=g_ctx_y;if(x+w>VGA_W)x=VGA_W-w;if(y+h>SCREEN_H)y=SCREEN_H-h;
    uint8_t panel=theme_get_color_idx(COLOR_BG_MENU),hover=theme_get_color_idx(COLOR_HOVER),fg=theme_get_color_idx(COLOR_FG),border=theme_get_color_idx(COLOR_BORDER);
    fill_round(x,y,x+w,y+h,18,panel);rect_round(x,y,x+w,y+h,18,border);
    for(int i=0;i<g_nctx;i++){int iy=y+10+i*40;int hv=mx>=x+8&&mx<=x+w-8&&my>=iy&&my<iy+36;if(hv)fill_round(x+8,iy,x+w-8,iy+36,12,hover);txt(x+24,iy+7,g_ctx[i].label,fg,hv?hover:panel);}
}
