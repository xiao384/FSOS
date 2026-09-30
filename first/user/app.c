// app.c - 应用界面: 登录 / 主界面 / 用户管理
#include "app.h"
#include "vga.h"
#include "cjk.h"        // ui_polish: 中文文案绘制
#include "gui.h"
#include "kb.h"
#include "user.h"
#include "terminal.h"
#include "wm.h"
#include "taskmgr.h"
#include "desktop.h"
#include "icon.h"
#include "perm.h"
#include "theme_api.h"
#include "theme.h"       // 有效身份: 登录后切换为所登录用户角色
#include "event_queue.h" // 事件队列 (登录界面事件驱动)

// hires: 比例缩放宏 (320x200 基准, 高分辨率下按比例放大)
#define SX(x) ((x) * VGA_W / 320)
#define SY(y) ((y) * SCREEN_H / 200)

// ============================================================
// 自动登录演示模式 (便于直接查看桌面 UI)
//   置 1: 启动跳过登录界面, 自动以第一个 root/admin 用户登录,
//         并直接进入图形桌面 wm_demo_run() (与主菜单按 [G] 相同)。
//   置 0: 传统流程: 登录界面 -> 主菜单 (主菜单内仍可 [G] 进桌面)。
// 桌面内注销/退出后均回到登录界面。
// ============================================================
#define PSBX_AUTO_DESKTOP 1

static int g_session = -1;   // 已登录用户索引, -1 = 未登录

// 当前会话显示名 (控制中心/面板使用); 会话状态在此模块维护
const char* user_session_name(void) {
    const UserRec* u = (g_session >= 0) ? user_get(g_session) : 0;
    return (u && u->name[0]) ? u->name : "Administrator";
}

static const char* role_str(uint8_t role) {
    if (role == ROLE_ROOT)  return "root";
    if (role == ROLE_ADMIN) return "admin";
    return "user";
}

// ---- 登录界面布局 (T5.1/T5.2) ----
typedef struct {
    int card_x, card_y, card_w, card_h;
    int brand_y, list_y, list_h, pass_y, btn_y, tip_y;
    int item_h, item_gap;
} login_layout_t;

typedef enum {
    LOGIN_HIT_NONE = 0,
    LOGIN_HIT_USER_ITEM,
    LOGIN_HIT_PASS_FIELD,
    LOGIN_HIT_LOGIN_BTN
} login_hit_kind_t;

typedef struct {
    login_hit_kind_t kind;
    int item;
} login_hit_t;

// ============================================================
// 登录界面
// ============================================================
static void login_round(int x0,int y0,int x1,int y1,int r,uint8_t c){
    if (gfx_is_lfb()) {
        uint8_t rr,gg,bb; gfx_idx_rgb(c,&rr,&gg,&bb);
        gfx_fill_round_rgb_aa(x0,y0,x1,y1,r,rr,gg,bb);
    } else vga_fill_round_rect(x0,y0,x1,y1,r,c);
}

static void login_measure(login_layout_t* L) {
    int cnt = user_count();
    if (cnt > 8) cnt = 8;
    if (cnt < 1) cnt = 1;

    L->item_h = 18;
    L->item_gap = 4;

    int cw = VGA_W * 3 / 8;
    if (cw < 280) cw = 280;
    if (cw > VGA_W - 80) cw = VGA_W - 80;
    if (cw > 420) cw = 420;
    L->card_w = cw;
    L->card_x = (VGA_W - cw) / 2;

    int brand_h = 36;
    int list_h = cnt * (L->item_h + L->item_gap);
    int info_h = 16;
    int pass_h = 22;
    int btn_h = 20;
    int tip_h = 16;
    int pad = 14;

    int ch = pad + brand_h + list_h + info_h + pass_h + 12 + btn_h + 12 + tip_h + pad;
    if (ch > SCREEN_H - 40) ch = SCREEN_H - 40;
    if (ch < 200) ch = 200;
    L->card_h = ch;
    L->card_y = (SCREEN_H - ch) / 2;

    int y = L->card_y + pad;
    L->brand_y = y;
    y += brand_h;
    L->list_y = y;
    L->list_h = list_h;
    y += list_h + info_h;
    L->pass_y = y;
    y += pass_h + 12;
    L->btn_y = y;
    y += btn_h + 12;
    L->tip_y = y;
}

static login_hit_t login_hit_test(int mx, int my, const login_layout_t* L) {
    login_hit_t h = { LOGIN_HIT_NONE, -1 };
    int cnt = user_count();
    if (cnt > 8) cnt = 8;

    for (int i = 0; i < cnt; i++) {
        int y0 = L->list_y + i * (L->item_h + L->item_gap);
        int y1 = y0 + L->item_h;
        if (mx >= L->card_x + 16 && mx < L->card_x + L->card_w - 16 &&
            my >= y0 && my < y1) {
            h.kind = LOGIN_HIT_USER_ITEM;
            h.item = i;
            return h;
        }
    }

    if (mx >= L->card_x + 16 && mx < L->card_x + L->card_w - 16 &&
        my >= L->pass_y && my < L->pass_y + 22) {
        h.kind = LOGIN_HIT_PASS_FIELD;
        return h;
    }

    if (mx >= L->card_x + L->card_w - 86 && mx < L->card_x + L->card_w - 16 &&
        my >= L->btn_y && my < L->btn_y + 20) {
        h.kind = LOGIN_HIT_LOGIN_BTN;
        return h;
    }

    return h;
}

static void login_draw_brand(const login_layout_t* L, uint8_t fg, uint8_t soft,
                             uint8_t panel, uint8_t accent) {
    int bx = L->card_x + 18;
    int by = L->brand_y;

    login_round(bx, by, bx + 16, by + 16, 3, accent);
    cjk_text(bx + 5, by + 4, "F", COL_WHITE, accent);
    cjk_text(bx + 24, by + 1, "FSOS", fg, panel);
    cjk_text(L->card_x + L->card_w - 58, by + 1, "v0.2", soft, panel);
    cjk_text(bx, by + 18, "登录到 FSOS", soft, panel);
}

static void draw_login_list(int sel, int state, int passlen, const char* msg) {
    uint8_t bg = theme_get_color_idx(COLOR_BG);
    uint8_t panel = theme_get_color_idx(COLOR_BG_PANEL);
    uint8_t field = theme_get_color_idx(COLOR_FIELD);
    uint8_t hover = theme_get_color_idx(COLOR_HOVER);
    uint8_t border = theme_get_color_idx(COLOR_BORDER);
    uint8_t accent = theme_get_color_idx(COLOR_ACCENT);
    uint8_t fg = theme_get_color_idx(COLOR_FG);
    uint8_t soft = theme_get_color_idx(COLOR_FG_SOFT);
    vga_clear(bg);

    if (gfx_is_lfb()) {
        uint8_t r1,g1,b1,r2,g2,b2;
        gfx_idx_rgb(COL_WALL_A,&r1,&g1,&b1); gfx_idx_rgb(COL_WALL_F,&r2,&g2,&b2);
        gfx_gradient_v_rgb(0,0,VGA_W-1,SCREEN_H-1,r1,g1,b1,r2,g2,b2);
    } else {
        vga_gradient_v(COL_WALL_A, 0, VGA_W-1, SCREEN_H-1, COL_WALL_A, COL_WALL_F);
    }

    login_layout_t L;
    login_measure(&L);
    int cx = L.card_x, cy = L.card_y, cw = L.card_w, ch = L.card_h;

    login_round(cx+3,cy+5,cx+cw+3,cy+ch+5,RADIUS_WINDOW,COL_SHADOW);
    login_round(cx,cy,cx+cw,cy+ch,RADIUS_WINDOW,panel);
    if (gfx_is_lfb()) {
        uint8_t pr,pg,pb; gfx_idx_rgb(panel,&pr,&pg,&pb);
        gfx_gradient_v_rgb(cx,cy,cx+cw,cy+ch,pr,pg,pb,6,18,34);
    }
    if (gfx_is_lfb()) {
        uint8_t br,bg2,bb; gfx_idx_rgb(border,&br,&bg2,&bb);
        gfx_round_rect_rgb_aa(cx,cy,cx+cw,cy+ch,RADIUS_WINDOW,br,bg2,bb);
    } else vga_draw_round_rect(cx,cy,cx+cw,cy+ch,RADIUS_WINDOW,border);

    login_draw_brand(&L, fg, soft, panel, accent);

    int cnt=user_count();
    uint8_t focus = theme_get_color_idx(COLOR_BORDER_FOCUS);
    for(int i=0;i<cnt && i<8;i++){
        const UserRec* u=user_get(i);
        int y=L.list_y+i*(L.item_h+L.item_gap);
        uint8_t rb=(i==sel)?hover:field;
        if(i==sel) login_round(cx+16,y,cx+cw-16,y+L.item_h,RADIUS_CTRL,rb);
        if(i==sel) cjk_text(cx+20,y+4,">",fg,rb);
        cjk_text(cx+30,y+4,u->name,i==sel?fg:soft,i==sel?rb:panel);
        cjk_text(cx+cw-62,y+4,role_str(u->role),i==sel?accent:soft,i==sel?rb:panel);
        if(i==sel && state==0){
            if(gfx_is_lfb()){
                uint8_t fr,fg2,fb; gfx_idx_rgb(focus,&fr,&fg2,&fb);
                gfx_round_rect_rgb_aa(cx+15,y-1,cx+cw-15,y+L.item_h+1,RADIUS_CTRL,fr,fg2,fb);
            } else vga_draw_round_rect(cx+15,y-1,cx+cw-15,y+L.item_h+1,RADIUS_CTRL,focus);
        }
    }

    int info_y=L.pass_y-16;
    cjk_text(cx+18,info_y,state==0?"选择用户后输入密码":"输入密码后按回车",soft,panel);
    login_round(cx+16,L.pass_y,cx+cw-16,L.pass_y+22,RADIUS_CTRL,field);
    if(state==1){
        char stars[16]; int n=passlen<14?passlen:14;
        for(int i=0;i<n;i++) stars[i]='*'; stars[n]=0;
        cjk_text(cx+24,L.pass_y+7,stars,fg,field);
        int curx=cx+24+n*8;
        login_round(curx,L.pass_y+8,curx+6,L.pass_y+16,1,fg);
        if(gfx_is_lfb()){
            uint8_t fr,fg2,fb; gfx_idx_rgb(focus,&fr,&fg2,&fb);
            gfx_round_rect_rgb_aa(cx+15,L.pass_y-1,cx+cw-15,L.pass_y+23,RADIUS_CTRL,fr,fg2,fb);
        } else vga_draw_round_rect(cx+15,L.pass_y-1,cx+cw-15,L.pass_y+23,RADIUS_CTRL,focus);
    } else cjk_text(cx+24,L.pass_y+7,"按 Enter 输入密码",soft,field);

    login_round(cx+cw-86,L.btn_y,cx+cw-16,L.btn_y+20,RADIUS_CTRL,accent);
    cjk_text(cx+cw-69,L.btn_y+6,"登录",COL_WHITE,accent);
    if(state==1){
        if(gfx_is_lfb()){
            uint8_t fr,fg2,fb; gfx_idx_rgb(focus,&fr,&fg2,&fb);
            gfx_round_rect_rgb_aa(cx+cw-87,L.btn_y-1,cx+cw-15,L.btn_y+21,RADIUS_CTRL,fr,fg2,fb);
        } else vga_draw_round_rect(cx+cw-87,L.btn_y-1,cx+cw-15,L.btn_y+21,RADIUS_CTRL,focus);
    }
    if(msg && msg[0]) cjk_text(cx+18,L.btn_y+6,msg,theme_get_color_idx(COLOR_DANGER),panel);
    cjk_text(cx+18,L.tip_y,"Up/Down选择 Enter确认 Esc返回",soft,panel);
    gfx_flip();   // gfx 为双缓冲: 登录界面直接写后台缓冲, 需翻页上屏
}

static void screen_login_legacy(void) {
    int sel = 0;
    int state = 0;            // 0 = 选择用户, 1 = 输入密码
    char pass[16];
    int passlen = 0;

    const char* m0 = "选择用户，按回车登录";
    const char* m1 = "输入密码，回车确认";

    for (;;) {
        if (sel >= user_count()) sel = 0;
        draw_login_list(sel, state, passlen,
                        state == 0 ? m0 : m1);
        int k = kb_wait();

        if (state == 0) {
            if (k == KEY_UP) {
                if (sel > 0) sel--; else sel = user_count() - 1;
            } else if (k == KEY_DOWN) {
                sel = (sel + 1) % (user_count() ? user_count() : 1);
            } else if (k == KEY_ENTER && user_count() > 0) {
                state = 1;
                passlen = 0;
                pass[0] = '\0';
            }
        } else {
            if (k == KEY_ESC) {
                state = 0;
            } else if (k == KEY_BS) {
                if (passlen > 0) { passlen--; pass[passlen] = '\0'; }
            } else if (k >= 32 && k <= 126 && passlen < 14) {
                pass[passlen++] = (char)k;
                pass[passlen] = '\0';
            } else if (k == KEY_ENTER) {
                if (user_verify(sel, pass) == 0) {
                    g_session = sel;
                    return;
                } else {
                    draw_login_list(sel, state, passlen, "密码错误");
                    kb_wait();
                    state = 0;
                }
            }
        }
    }
}

static void screen_login(void) {
    int sel = 0;
    int state = 0;
    char pass[16];
    int passlen = 0;
    const char* msg = 0;

    event_queue_init();
    event_queue_sync_mouse();

    login_layout_t L;
    login_measure(&L);
    draw_login_list(sel, state, passlen, msg);

    for (;;) {
        if (sel >= user_count()) sel = 0;

        event_queue_poll_from_drivers();

        gui_event_t ev;
        if (!event_queue_pop(&ev)) {
            __asm__ volatile("hlt");
            continue;
        }

        int need_redraw = 0;

        if (ev.type == EV_KEY) {
            int k = ev.key;
            if (state == 0) {
                if (k == KEY_UP) {
                    if (sel > 0) sel--; else sel = user_count() - 1;
                    msg = 0; need_redraw = 1;
                } else if (k == KEY_DOWN) {
                    sel = (sel + 1) % (user_count() ? user_count() : 1);
                    msg = 0; need_redraw = 1;
                } else if (k == KEY_ENTER && user_count() > 0) {
                    state = 1; passlen = 0; pass[0] = '\0';
                    msg = 0; need_redraw = 1;
                } else if (k == KEY_ESC) {
                    return;
                }
            } else {
                if (k == KEY_ESC) {
                    state = 0; msg = 0; need_redraw = 1;
                } else if (k == KEY_BS) {
                    if (passlen > 0) { passlen--; pass[passlen] = '\0'; }
                    need_redraw = 1;
                } else if (k >= 32 && k <= 126 && passlen < 14) {
                    pass[passlen++] = (char)k; pass[passlen] = '\0';
                    need_redraw = 1;
                } else if (k == KEY_ENTER) {
                    if (user_verify(sel, pass) == 0) {
                        g_session = sel;
                        return;
                    } else {
                        msg = "密码错误"; state = 0; need_redraw = 1;
                    }
                }
            }
        } else if (ev.type == EV_MOUSE_LEFT && ev.pressed) {
            login_hit_t h = login_hit_test(ev.x, ev.y, &L);
            if (h.kind == LOGIN_HIT_USER_ITEM) {
                sel = h.item;
                if (state == 1) { state = 0; msg = 0; }
                need_redraw = 1;
            } else if (h.kind == LOGIN_HIT_LOGIN_BTN) {
                if (state == 1) {
                    if (user_verify(sel, pass) == 0) {
                        g_session = sel;
                        return;
                    } else {
                        msg = "密码错误"; state = 0; need_redraw = 1;
                    }
                } else if (user_count() > 0) {
                    state = 1; passlen = 0; pass[0] = '\0';
                    msg = 0; need_redraw = 1;
                }
            } else if (h.kind == LOGIN_HIT_PASS_FIELD) {
                if (state == 0 && user_count() > 0) {
                    state = 1; passlen = 0; pass[0] = '\0';
                    msg = 0; need_redraw = 1;
                }
            }
        }

        if (need_redraw) {
            draw_login_list(sel, state, passlen, msg);
        }
    }
}

// ============================================================
// 主界面 (登录后)
// ============================================================
static void screen_admin(void);   // 前向声明 (admin 管理界面)

static void screen_main(void) {
    char msg[64];
    msg[0] = '\0';
    for (;;) {
        const UserRec* u = user_get(g_session);
        if (!u) { g_session = -1; return; }
        perm_set_role(u->role);   // 跟踪当前有效身份

        vga_clear(COL_UI_BG_SOFT);
        gui_title_bar("FSOS", "v0.2");
        vga_fill_rect(20, 30, 300, 120, COL_PANEL);
        vga_draw_rect(20, 30, 300, 120, COL_LBLUE);
        {
            const char* welcome = "欢迎";
            int ww = cjk_text_w(welcome);
            cjk_text((VGA_W - ww) / 2, SY(40), welcome, COL_YELLOW, COL_PANEL);
        }
        vga_draw_text_center(58, u->name, COL_WHITE, COL_PANEL);
        {
            const char* role_lbl = "角色：";
            int rw = cjk_text_w(role_lbl);
            cjk_text((VGA_W - rw) / 2, 68, role_lbl, COL_LGRAY, COL_PANEL);
            vga_draw_text((VGA_W - rw) / 2 + rw, 72, role_str(u->role), COL_YELLOW, COL_PANEL);
        }

        // ---- 应用图标网格 (3 列 x 2 行) ----
        {
            struct mi { int key; int icon; const char* name; const char* label; };
            static const struct mi items[] = {
                {'T', ICON_TERMINAL, "ic_t", "终端"},
                {'M', ICON_USERMGR,  "ic_m", "用户管理"},
                {'G', ICON_DESKTOP,  "ic_g", "桌面"},
                {'D', ICON_DESKTOP,  "ic_d", "桌面++"},
                {'K', ICON_TASKMGR,  "ic_k", "任务管理"},
                {'L', ICON_LOCK,     "ic_l", "锁定"},
            };
            int col_w = VGA_W / 3;
            int y0 = SCREEN_H / 2 - 20;      // 图标区起始 y (动态居中)
            for (int i = 0; i < 6; i++) {
                int col = i % 3, row = i / 3;
                int ix = col_w * col + (col_w - ICON_SIZE) / 2;
                int iy = y0 + row * (ICON_SIZE + 28);
                icon_draw_auto(items[i].name, items[i].icon, ix, iy);
                int lw = cjk_text_w(items[i].label);
                cjk_text(ix + (ICON_SIZE - lw) / 2, iy + ICON_SIZE - 2,
                         items[i].label, COL_WHITE, COL_UI_BG_SOFT);
                char kh[4] = {'[', (char)items[i].key, ']', 0};
                int kw = vga_text_w(kh);
                vga_draw_text(ix + (ICON_SIZE - kw) / 2, iy + ICON_SIZE + 12,
                              kh, COL_YELLOW, COL_UI_BG_SOFT);
            }
        }
        gui_status_bar(msg, msg[0] ? COL_UI_SOFT_WARN : COL_LGRAY);

        int k = kb_wait();
        msg[0] = '\0';
        if (k == 't' || k == 'T') {
            terminal_run();          // 终端始终以 root(最高)权限运行
        } else if (k == 'g' || k == 'G') {
            wm_demo_run();           // 统一桌面: 窗口 + 终端 + 语言运行时
        } else if (k == 'd' || k == 'D') {
            desktop_run();           // C++ 入口: 委托到统一 WM (task40)
        } else if (k == 'm' || k == 'M') {
            if (u->role == ROLE_ADMIN || u->role == ROLE_ROOT) {
                screen_admin();
            } else {
                msg[0] = '\0';
                gui_status_bar("无权限：仅管理员", COL_UI_SOFT_ERR);
                kb_wait();
            }
        } else if (k == 'k' || k == 'K') {
            taskmgr_run();           // 任务管理器: 进程/性能/结束进程
        } else if (k == 'l' || k == 'L') {
            g_session = -1;
            return;
        }
    }
}

// ============================================================
// 用户管理界面 (admin)
// ============================================================
static void draw_admin_list(int sel) {
    vga_clear(COL_UI_BG_SOFT);
    gui_title_bar("用户管理", "admin");

    cjk_text(10, 14, "用户列表：", COL_LGRAY, COL_UI_BG_SOFT);
    int cnt = user_count();
    for (int i = 0; i < cnt && i < 9; i++) {
        const UserRec* u = user_get(i);
        int y = 30 + i * 16;
        uint8_t bg = (i == sel) ? COL_PANEL_HI : COL_PANEL;
        vga_fill_rect(10, y, 190, y + 14, bg);
        char line[32];
        line[0] = '\0';
        int j = 0;
        if (i == sel) { line[j++] = '>'; line[j++] = ' '; }
        for (int c = 0; u->name[c] && j < 20; c++) line[j++] = u->name[c];
        line[j] = '\0';
        vga_draw_text(14, y + 3, line, COL_WHITE, bg);
        vga_draw_text(SX(150), y + 3, role_str(u->role),
                      (i == sel) ? COL_YELLOW : COL_LGRAY, bg);
    }

    // 右侧信息
    vga_fill_rect(SX(200), SY(18), SX(310), SY(150), COL_PANEL);
    vga_draw_rect(SX(200), SY(18), SX(310), SY(150), COL_LBLUE);
    if (sel < cnt) {
        const UserRec* u = user_get(sel);
        cjk_text(206, 22, "已选：", COL_LGRAY, COL_PANEL);
        vga_draw_text(206, 38, u->name, COL_WHITE, COL_PANEL);
        cjk_text(206, 48, "角色：", COL_LGRAY, COL_PANEL);
        vga_draw_text(206, 64, role_str(u->role), COL_YELLOW, COL_PANEL);
    }

    cjk_text(10, 172, "上/下 选 A:增 E:改 D:删 L:注销",
             COL_LGRAY, COL_UI_BG_SOFT);
}

// 添加用户对话框
static void screen_admin_add(void) {
    char name[16], pass[16], rolec[2];
    int nlen = 0, plen = 0, rlen = 0;
    DialogField fields[3];
    fields[0].label = "用户名："; fields[0].buf = name; fields[0].len = &nlen;
    fields[0].max = 14; fields[0].secret = 0;
    fields[1].label = "密码："; fields[1].buf = pass; fields[1].len = &plen;
    fields[1].max = 14; fields[1].secret = 1;
    fields[2].label = "管理员(1/0):"; fields[2].buf = rolec; fields[2].len = &rlen;
    fields[2].max = 1; fields[2].secret = 0;

    if (gui_dialog_form("添加用户", fields, 3, "确定", "取消")) {
        uint8_t role = (rlen > 0 && rolec[0] == '1') ? ROLE_ADMIN : ROLE_NORMAL;
        int r = user_add(name, pass, role);
        if (r == 0) {
            user_save();
            gui_status_bar("已添加并保存", COL_UI_SOFT_OK);
        } else if (r == -2) {
            gui_status_bar("错误：用户名已存在", COL_UI_SOFT_ERR);
        } else {
            gui_status_bar("错误：无法添加", COL_UI_SOFT_ERR);
        }
        kb_wait();
    }
}

// 编辑用户对话框
static void screen_admin_edit(int sel) {
    const UserRec* u = user_get(sel);
    if (!u) return;
    char name[16], pass[16], rolec[2];
    int nlen = 0, plen = 0, rlen = 0;
    while (u->name[nlen] && nlen < 14) { name[nlen] = u->name[nlen]; nlen++; }
    name[nlen] = '\0';
    while (u->pass[plen] && plen < 14) { pass[plen] = u->pass[plen]; plen++; }
    pass[plen] = '\0';
    rolec[0] = (u->role == ROLE_ADMIN) ? '1' : '0';
    rolec[1] = '\0';
    rlen = 1;

    DialogField fields[3];
    fields[0].label = "用户名："; fields[0].buf = name; fields[0].len = &nlen;
    fields[0].max = 14; fields[0].secret = 0;
    fields[1].label = "密码："; fields[1].buf = pass; fields[1].len = &plen;
    fields[1].max = 14; fields[1].secret = 1;
    fields[2].label = "管理员(1/0):"; fields[2].buf = rolec; fields[2].len = &rlen;
    fields[2].max = 1; fields[2].secret = 0;

    if (gui_dialog_form("编辑用户", fields, 3, "确定", "取消")) {
        if (user_rename(sel, name) == 0) {
            user_setpass(sel, pass);
            uint8_t role = (rlen > 0 && rolec[0] == '1') ? ROLE_ADMIN : ROLE_NORMAL;
            // 若用户降级且是当前登录用户, 保持其会话但注意: 这里仅保存
            user_setrole(sel, role);
            user_save();
            gui_status_bar("已更新并保存", COL_UI_SOFT_OK);
        } else {
            gui_status_bar("错误：用户名无效或重复", COL_UI_SOFT_ERR);
        }
        kb_wait();
    }
}

static void screen_admin(void) {
    int sel = 0;
    for (;;) {
        if (sel >= user_count()) sel = user_count() - 1;
        if (sel < 0) sel = 0;
        draw_admin_list(sel);
        int k = kb_wait();

        if (k == KEY_UP && sel > 0) sel--;
        else if (k == KEY_DOWN && sel < user_count() - 1) sel++;
        else if (k == 'a' || k == 'A') screen_admin_add();
        else if (k == 'e' || k == 'E') screen_admin_edit(sel);
        else if (k == 'd' || k == 'D') {
            const UserRec* du = user_get(sel);
            if (user_count() <= 1) {
                gui_status_bar("错误：至少需保留 1 个用户", COL_UI_SOFT_ERR);
                kb_wait();
            } else if (du && gui_dialog_confirm("删除用户",
                                                du->name,
                                                "删除", "取消")) {
                int r = user_remove(sel);
                if (r == 0) {
                    user_save();
                    if (sel >= user_count()) sel = user_count() - 1;
                    gui_status_bar("已删除并保存", COL_UI_SOFT_OK);
                } else if (r == -2) {
                    gui_status_bar("错误：不能删除最后的管理员", COL_UI_SOFT_ERR);
                } else {
                    gui_status_bar("错误：无法删除", COL_UI_SOFT_ERR);
                }
                kb_wait();
            }
        } else if (k == 'l' || k == 'L') {
            g_session = -1;
            return;
        }
    }
}

// ============================================================
// 主循环
// ============================================================
void app_run(void) {
#if PSBX_AUTO_DESKTOP
    // 自动登录: 取第一个 root/admin 用户 (优先 admin), 不存在则取首个用户
    {
        int idx = -1;
        int cnt = user_count();
        for (int i = 0; i < cnt; i++) {
            const UserRec* u = user_get(i);
            if (u && (u->role == ROLE_ROOT || u->role == ROLE_ADMIN)) { idx = i; break; }
        }
        if (idx < 0 && cnt > 0) idx = 0;
        if (idx >= 0) {
            g_session = idx;
            perm_set_role(user_get(idx)->role);
            wm_demo_run();        // 直接进入图形桌面 (等价主菜单按 G)
            g_session = -1;       // 桌面注销/退出后回到登录
        }
    }
#endif
    for (;;) {
        if (g_session < 0) {
            screen_login();
        } else {
            const UserRec* u = user_get(g_session);
            if (!u) { g_session = -1; continue; }
            perm_set_role(u->role);
            wm_demo_run();
            // wm_demo_run() ends only for logout/desktop exit. The next loop always
            // presents the real login surface, then creates a fresh desktop session.
            g_session = -1;
        }
    }
}
