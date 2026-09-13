// installer.c - 图形化安装向导 (mode13h, 双缓冲 gfx_*)
//
// 目标: 提供"像 Windows 一样"的多步骤图形安装体验。
// 流程:
//   欢迎 -> 用户账户 -> 主机名 -> 磁盘确认 -> 写入进度 -> 完成重启
//
// 交互: 同时支持鼠标点击与键盘。
//   - 鼠标: 移动光标, 左键点击按钮/文本框聚焦, 再次点击已聚焦文本框开始输入。
//   - 键盘: Tab / 方向键 切换焦点, Enter 进入下一步/确认, Esc 取消(重启不写盘)。
//   文本框聚焦后直接键入字符(密码框显示 '*')。
//
// 写入前会在内存镜像副本里补上: 用户库(超级块 + 管理员 + 可选 guest)
// 与 sysconf 的主机名, 使安装出的系统直接带有自定义账户与主机名。

#include "installer.h"
#include "gfx.h"
#include "vga.h"        // COL_* 调色板索引, FONT_W
#include "kb.h"
#include "mouse.h"
#include "ata.h"
#include "io.h"
#include "layout.h"
#include <stdint.h>

extern const uint8_t  install_image[];
extern const uint32_t install_image_size;   // 镜像字节数

#define SECTOR      512
#define MARKER_LBA  (DISK_SECTORS - 1)               // 4095: 安装标记扇区
#define HOST_OFF    (LBA_SYSCONF * SECTOR + 8)        // sysconf.hostname
#define SB_OFF      (LBA_USER_SB  * SECTOR)           // 用户库超级块
#define REC_OFF     (LBA_USER_REC * SECTOR)           // 用户记录区
#define ROOT_ROLE   2                                 // ROLE_ROOT
#define ADMIN_ROLE 1
#define GUEST_ROLE 0

// ---- 屏幕常量 (hires: 动态适配分辨率) ----
#define SW VGA_W
#define SH SCREEN_H
#define BG  COL_BLUE
// hires: 比例缩放宏 (320x200 基准)
#define SX(x) ((x) * SW / 320)
#define SY(y) ((y) * SH / 200)

// ---- 控件类型 ----
typedef enum { CT_BUTTON, CT_TEXT, CT_CHECK } ctl_type;
typedef struct {
    ctl_type type;
    int   x, y, w;
    const char* label;     // 按钮文字 / 文本框前的提示
    char* buf;             // 文本框编辑缓冲 (CT_TEXT)
    int   max;             // 文本框最大长度
    int   hide;            // 密码隐藏
    int   checked;         // CT_CHECK 状态
    int   enabled;         // 0 = 灰显不可点
} ctl_t;

// ---- 全局向导状态 ----
static int      g_focus = 0;
static ctl_t*   g_ctls[16];
static int      g_nctl = 0;
static uint8_t  g_cur_btn = COL_LGREEN;   // 当前按钮配色基

// ---- 小工具 ----
// 把 src 拼到 dst 末尾 (dst 以 '\0' 结尾), 返回写入的字符数。
static int sprintf_cat(char* dst, const char* src) {
    int n = 0; while (dst[n]) n++;          // 找到当前结尾
    int w = 0;
    while (src[w]) { dst[n++] = src[w++]; }
    dst[n] = 0;
    return w;
}

static void reboot(void) {
    for (volatile int i = 0; i < 2000000; i++) { }
    outb(0x64, 0xFE);                 // 8042 键盘控制器复位
    for (;;) __asm__ volatile("cli; hlt");
}

// 控件索引辅助 (需用全局数组下标)。声明在前, 定义见文件下方。
static int ctl_index(ctl_t* b);

static void fill(int x0, int y0, int x1, int y1, uint8_t c) {
    gfx_fill_idx(x0, y0, x1, y1, c);
}
static void rect(int x0, int y0, int x1, int y1, uint8_t c) {
    gfx_rect_idx(x0, y0, x1, y1, c);
}
static void text(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    gfx_text_idx(x, y, s, fg, bg);
}
static int text_w(const char* s) {
    int w = 0;
    int sc = gfx_font_scale();
    while (*s) { w += FONT_W * sc; s++; }
    return w;
}

static void title(void) {
    fill(0, 0, SW - 1, 13, COL_TITLEBG);
    rect(0, 0, SW - 1, 13, COL_LBLUE);
    text(6, 4, "FSOS (Freedom Security OS)", COL_WHITE, COL_TITLEBG);
    text(SW - 44, 4, "v1", COL_LGRAY, COL_TITLEBG);
}

// 绘制一个按钮 (focus 时高亮)
static void draw_button(ctl_t* b) {
    uint8_t bg = (b->enabled == 0) ? COL_DGRAY : (g_focus == ctl_index(b) ? COL_PANEL_HI : COL_PANEL);
    uint8_t fg = (b->enabled == 0) ? COL_DGRAY : (g_focus == ctl_index(b) ? COL_WHITE : COL_LGRAY);
    fill(b->x, b->y, b->x + b->w, b->y + 16, bg);
    rect(b->x, b->y, b->x + b->w, b->y + 16,
         (b->enabled == 0) ? COL_DGRAY : (g_focus == ctl_index(b) ? COL_LBLUE : COL_LGRAY));
    int tw = text_w(b->label);
    text(b->x + (b->w - tw) / 2, b->y + 4, b->label, fg, bg);
}

// 绘制文本框 (含标签 + 编辑区)
static void draw_text(ctl_t* b) {
    int lx = b->x, ly = b->y;
    if (b->label && *b->label) {
        text(lx, ly, b->label, COL_LGRAY, BG);
        lx += text_w(b->label) + 4;
    }
    int ex = lx, ey = ly;
    int ew = b->w;
    fill(ex, ey, ex + ew, ey + 12, COL_FIELD);
    rect(ex, ey, ex + ew, ey + 12,
         (g_focus == ctl_index(b)) ? COL_WHITE : COL_DGRAY);
    int len = 0; while (b->buf[len]) len++;
    int cx = ex + 3;
    for (int i = 0; i < len; i++) {
        char c = b->hide ? '*' : b->buf[i];
        char tmp[2] = { c, 0 };
        text(cx, ey + 2, tmp, COL_BLACK, COL_FIELD);
        cx += FONT_W * gfx_font_scale();
    }
    if (g_focus == ctl_index(b)) {
        int ccur = ex + 3 + len * FONT_W * gfx_font_scale();
        text(ccur, ey + 2, "_", COL_BLACK, COL_FIELD);
    }
}

static void draw_check(ctl_t* b) {
    int bx = b->x, by = b->y;
    fill(bx, by, bx + 11, by + 11, COL_FIELD);
    rect(bx, by, bx + 11, by + 11, (g_focus == ctl_index(b)) ? COL_WHITE : COL_DGRAY);
    if (b->checked) {
        text(bx + 2, by + 1, "X", COL_BLACK, COL_FIELD);
    }
    text(bx + 16, by + 1, b->label, COL_LGRAY, BG);
}

// 绘制鼠标光标 (简单箭头)
static void draw_cursor(int mx, int my) {
    uint8_t c = COL_WHITE;
    for (int i = 0; i < 9; i++) gfx_pixel_idx(mx + i, my + 0, c);
    for (int i = 0; i < 8; i++) gfx_pixel_idx(mx + i, my + 1 + i, c);
    gfx_pixel_idx(mx + 1, my + 1, COL_BLACK);
    gfx_pixel_idx(mx + 0, my + 1, COL_BLACK);
}

// 控件索引辅助 (需用全局数组下标)
static int ctl_index(ctl_t* b) {
    for (int i = 0; i < g_nctl; i++) if (g_ctls[i] == b) return i;
    return -1;
}

// 命中测试: 返回命中的控件下标, 无则 -1
static int hit_test(int mx, int my) {
    for (int i = 0; i < g_nctl; i++) {
        ctl_t* b = g_ctls[i];
        if (b->enabled == 0) continue;
        int x1 = b->x + b->w, y1 = b->y + 16;
        if (mx >= b->x && mx <= x1 && my >= b->y && my <= y1) return i;
    }
    return -1;
}

// 重新绘制整个控件集 (调用方先清屏)
static void draw_ctls(void) {
    for (int i = 0; i < g_nctl; i++) {
        ctl_t* b = g_ctls[i];
        if (b->type == CT_BUTTON) draw_button(b);
        else if (b->type == CT_TEXT) draw_text(b);
        else if (b->type == CT_CHECK) draw_check(b);
    }
}

// 通用事件循环: 返回被选中的按钮索引, 或 -2 (Esc 取消)
// 鼠标左键点击按钮即触发; 键盘 Enter 触发当前焦点按钮; Tab/方向键切焦点。
static int run_page(void) {
    mouse_state_t m;
    int last_btn = 0;
    for (;;) {
        int k = kb_poll();
        if (k == KEY_TAB || k == KEY_DOWN) {
            // 找下一个 enabled 控件
            int i = g_focus;
            for (int n = 0; n < g_nctl; n++) {
                i = (i + 1) % g_nctl;
                if (g_ctls[i]->enabled) { g_focus = i; break; }
            }
        } else if (k == KEY_UP) {
            int i = g_focus;
            for (int n = 0; n < g_nctl; n++) {
                i = (i - 1 + g_nctl) % g_nctl;
                if (g_ctls[i]->enabled) { g_focus = i; break; }
            }
        } else if (k == KEY_ENTER) {
            return g_focus;
        } else if (k == 27) {            // ESC
            return -2;
        } else if (k == ' ' && g_ctls[g_focus]->type == CT_CHECK) {
            g_ctls[g_focus]->checked ^= 1;
        }

        mouse_get(&m);
        int h = hit_test(m.x, m.y);
        if (m.left && !last_btn) {       // 左键按下沿
            if (h >= 0) {
                g_focus = h;
                if (g_ctls[h]->type == CT_CHECK) {
                    g_ctls[h]->checked ^= 1;
                } else {
                    return h;            // 按钮点击直接确认
                }
            }
        }
        last_btn = m.left;

        gfx_flip();
        __asm__ volatile("pause");
    }
}

// 在已聚焦文本框里接收输入 (直到 Enter/Esc)。返回 0 正常, -2 取消整页。
static int edit_focused(void) {
    ctl_t* b = g_ctls[g_focus];
    if (b->type != CT_TEXT) return 0;
    for (;;) {
        int len = 0; while (b->buf[len]) len++;
        // 重画本框
        draw_text(b);
        gfx_flip();
        int k = kb_poll();
        if (k == KEY_ENTER || k == 27) return 0;
        if (k == KEY_TAB || k == KEY_DOWN || k == KEY_UP) return 0;
        if (k == '\b') { if (len > 0) b->buf[--len] = 0; }
        else if (k >= 32 && k < 127 && len < b->max - 1) b->buf[len++] = (char)k;
    }
}

// ---- 用户记录写入 ----
static void set_rec(uint8_t* base, int idx, uint8_t role,
                    const char* name, const char* pass) {
    uint8_t* r = base + idx * 32;
    r[0] = 0x55;            // magic
    r[1] = role;
    for (int i = 0; i < 15; i++) { r[2 + i] = 0; r[17 + i] = 0; }
    for (int i = 0; name[i] && i < 14; i++) r[2 + i] = (uint8_t)name[i];
    for (int i = 0; pass[i] && i < 14; i++) r[17 + i] = (uint8_t)pass[i];
}
static void cpy(uint8_t* dst, const char* src, int n) {
    for (int i = 0; i < n && src[i]; i++) dst[i] = (uint8_t)src[i];
}

// ---- 各页 ----

// 页 0: 欢迎
static int page_welcome(void) {
    fill(0, 0, SW - 1, SH - 1, BG);
    title();
    text(40, 50, "Welcome to FSOS Setup", COL_WHITE, BG);
    text(40, 70, "This wizard will install the operating system", COL_LGRAY, BG);
    text(40, 82, "onto your hard disk.", COL_LGRAY, BG);
    text(40, 104, "WARNING: all data on the target disk will be erased.", COL_YELLOW, BG);
    text(40, 130, "Click 'Next' or press ENTER to continue.", COL_LGREEN, BG);
    text(40, 144, "ESC to cancel (reboot without changes).", COL_DGRAY, BG);

    g_nctl = 0;
    static ctl_t btn_next = { CT_BUTTON, 230, 165, 70, "Next", 0, 0, 0, 0, 1 };
    g_ctls[g_nctl++] = &btn_next;
    g_focus = 0;

    draw_ctls();
    mouse_state_t m; mouse_get(&m); draw_cursor(m.x, m.y); gfx_flip();
    return run_page();
}

// 页 1: 用户账户 (管理员 + 可选 guest)
static int page_account(char* admin, char* adminpw, ctl_t* guest_chk) {
    fill(0, 0, SW - 1, SH - 1, BG);
    title();
    text(SX(40), SY(26), "Create your administrator account", COL_WHITE, BG);

    g_nctl = 0;
    static ctl_t t_admin = { CT_TEXT, 40, 50, 150, "Admin :", 0, 0, 0, 0, 1 };
    t_admin.x = SX(40); t_admin.y = SY(50); t_admin.w = SX(150);
    t_admin.buf = admin; t_admin.max = 16;
    static ctl_t t_adminpw = { CT_TEXT, 40, 78, 150, "Password :", 0, 0, 1, 0, 1 };
    t_adminpw.x = SX(40); t_adminpw.y = SY(78); t_adminpw.w = SX(150);
    t_adminpw.buf = adminpw; t_adminpw.max = 16;
    static ctl_t chk_guest = { CT_CHECK, 40, 108, 0, "Create a guest account", 0, 0, 0, 0, 1 };
    chk_guest.x = SX(40); chk_guest.y = SY(108);
    *guest_chk = chk_guest;
    static ctl_t btn_next = { CT_BUTTON, 230, 165, 70, "Next", 0, 0, 0, 0, 1 };
    btn_next.x = SX(230); btn_next.y = SY(165); btn_next.w = SX(70);
    static ctl_t btn_back = { CT_BUTTON, 150, 165, 70, "Back", 0, 0, 0, 0, 1 };
    btn_back.x = SX(150); btn_back.y = SY(165); btn_back.w = SX(70);
    g_ctls[g_nctl++] = &t_admin;
    g_ctls[g_nctl++] = &t_adminpw;
    g_ctls[g_nctl++] = guest_chk;
    g_ctls[g_nctl++] = &btn_back;
    g_ctls[g_nctl++] = &btn_next;
    g_focus = 0;

    for (;;) {
        draw_ctls();
        mouse_state_t m; mouse_get(&m); draw_cursor(m.x, m.y); gfx_flip();
        int r = run_page();
        if (r == -2) return -2;
        if (r == ctl_index(&t_admin) || r == ctl_index(&t_adminpw)) {
            g_focus = r; edit_focused(); continue;
        }
        if (r == ctl_index(&btn_back)) return 0;   // 回欢迎
        if (r == ctl_index(&btn_next)) {
            if (admin[0] == 0) { text(40, 130, "Admin name required.", COL_RED, BG); continue; }
            if (adminpw[0] == 0) { text(40, 130, "Admin password required.", COL_RED, BG); continue; }
            return 2;   // 去主机名页
        }
    }
}

// 页 2: 主机名
static int page_hostname(char* host) {
    fill(0, 0, SW - 1, SH - 1, BG);
    title();
    text(40, 30, "Computer name", COL_WHITE, BG);
    text(40, 50, "This name identifies the computer on a network.", COL_LGRAY, BG);

    g_nctl = 0;
    static ctl_t t_host = { CT_TEXT, 40, 74, 160, "Hostname :", 0, 0, 0, 0, 1 };
    t_host.buf = host; t_host.max = 16;
    static ctl_t btn_next = { CT_BUTTON, 230, 165, 70, "Next", 0, 0, 0, 0, 1 };
    static ctl_t btn_back = { CT_BUTTON, 150, 165, 70, "Back", 0, 0, 0, 0, 1 };
    g_ctls[g_nctl++] = &t_host;
    g_ctls[g_nctl++] = &btn_back;
    g_ctls[g_nctl++] = &btn_next;
    g_focus = 0;

    for (;;) {
        draw_ctls();
        mouse_state_t m; mouse_get(&m); draw_cursor(m.x, m.y); gfx_flip();
        int r = run_page();
        if (r == -2) return -2;
        if (r == ctl_index(&t_host)) { g_focus = r; edit_focused(); continue; }
        if (r == ctl_index(&btn_back)) return 1;
        if (r == ctl_index(&btn_next)) {
            if (host[0] == 0) { text(40, 100, "Hostname required.", COL_RED, BG); continue; }
            return 3;
        }
    }
}

// 页 3: 磁盘确认 (显示目标盘)
static int page_diskinfo(int disk_ok, char* summary) {
    fill(0, 0, SW - 1, SH - 1, BG);
    title();
    text(40, 28, "Ready to install", COL_WHITE, BG);
    text(40, 48, "Review the installation summary:", COL_LGRAY, BG);

    // 摘要
    int y = 70;
    const char* lines[6];
    int n = 0;
    lines[n++] = summary;
    for (int i = 0; i < n; i++) text(40, y + i * 12, lines[i], COL_WHITE, BG);

    if (!disk_ok) {
        text(40, 150, "ERROR: no hard disk found.", COL_RED, BG);
    }

    g_nctl = 0;
    static ctl_t btn_install = { CT_BUTTON, 230, 165, 70, "Install", 0, 0, 0, 0, 1 };
    static ctl_t btn_back = { CT_BUTTON, 150, 165, 70, "Back", 0, 0, 0, 0, 1 };
    btn_install.enabled = disk_ok ? 1 : 0;
    g_ctls[g_nctl++] = &btn_back;
    g_ctls[g_nctl++] = &btn_install;
    g_focus = disk_ok ? ctl_index(&btn_install) : ctl_index(&btn_back);

    for (;;) {
        draw_ctls();
        mouse_state_t m; mouse_get(&m); draw_cursor(m.x, m.y); gfx_flip();
        int r = run_page();
        if (r == -2) return -2;
        if (r == ctl_index(&btn_back)) return 2;
        if (r == ctl_index(&btn_install) && disk_ok) return 4;
    }
}

// 页 4: 二次确认擦除 (防误操作: 必须再次点击 Install 才真正写盘)
static int page_confirm(void) {
    fill(0, 0, SW - 1, SH - 1, BG);
    title();
    text(40, 40, "Confirm disk erase", COL_WHITE, BG);
    text(40, 64, "WARNING: this will PERMANENTLY erase ALL", COL_YELLOW, BG);
    text(40, 76, "data on the target hard disk.", COL_YELLOW, BG);
    text(40, 100, "This operation cannot be undone.", COL_YELLOW, BG);
    text(40, 128, "Click 'Install' to proceed, or 'Back'/ESC to cancel.", COL_LGRAY, BG);

    g_nctl = 0;
    static ctl_t btn_install = { CT_BUTTON, 230, 165, 70, "Install", 0, 0, 0, 0, 1 };
    static ctl_t btn_back = { CT_BUTTON, 150, 165, 70, "Back", 0, 0, 0, 0, 1 };
    g_ctls[g_nctl++] = &btn_back;
    g_ctls[g_nctl++] = &btn_install;
    g_focus = ctl_index(&btn_back);

    for (;;) {
        draw_ctls();
        mouse_state_t m; mouse_get(&m); draw_cursor(m.x, m.y); gfx_flip();
        int r = run_page();
        if (r == -2) return -2;                    // ESC 取消不写盘
        if (r == ctl_index(&btn_back)) return 3;   // 回磁盘确认页
        if (r == ctl_index(&btn_install)) return 5; // 去写入页
    }
}

static void progress(int pct) {
    int x0 = 44, y0 = 120, x1 = 276, y1 = 134;
    rect(x0 - 1, y0 - 1, x1 + 1, y1 + 1, COL_WHITE);
    int w = (x1 - x0) * pct / 100;
    if (w > 0) fill(x0, y0, x0 + w, y1, COL_LGREEN);
    gfx_flip();
}

void installer_run(void) {

    mouse_init();

    // 状态缓冲
    char admin[16];   for (int i = 0; i < 16; i++) admin[i] = 0;
    char adminpw[16]; for (int i = 0; i < 16; i++) adminpw[i] = 0;
    char host[16];    for (int i = 0; i < 16; i++) host[i] = 0;
    const char* def = "fsos";
    for (int i = 0; def[i] && i < 15; i++) host[i] = def[i];
    ctl_t guest_chk = { CT_CHECK, 40, 108, 0, "Create a guest account", 0, 0, 0, 0, 1 };
    guest_chk.checked = 1;
    char summary[64];

    int page = 0;
    for (;;) {
        if (page == 0) {
            int r = page_welcome();
            if (r == -2) reboot();
            page = 1;
        } else if (page == 1) {
            int r = page_account(admin, adminpw, &guest_chk);
            if (r == -2) reboot();
            if (r == 0) page = 0; else page = 2;
        } else if (page == 2) {
            int r = page_hostname(host);
            if (r == -2) reboot();
            if (r == 1) page = 1; else page = 3;
        } else if (page == 3) {
            int disk_ok = (ata_probe() == 0);
            // 构造摘要 (一行)
            int n = 0;
            for (int i = 0; admin[i] && i < 14; i++) summary[n++] = admin[i];
            n += sprintf_cat(summary + n, " (admin)");
            n += sprintf_cat(summary + n, " | host: ");
            for (int i = 0; host[i] && i < 14; i++) summary[n++] = host[i];
            summary[n] = 0;
            int r = page_diskinfo(disk_ok, summary);
            if (r == -2) reboot();
            if (r == 2) page = 2; else page = 6;   // 去二次确认页
        } else if (page == 6) {
            int r = page_confirm();
            if (r == -2) reboot();
            if (r == 3) page = 3; else page = 5;   // 确认后去写入页
        } else if (page == 5) {
            // ---- 写入进度页 ----
            fill(0, 0, SW - 1, SH - 1, BG);
            title();
            text(40, 45, "Installing FSOS...", COL_WHITE, BG);
            progress(0);
            gfx_flip();

            if (ata_probe() != 0) {
                text(40, 150, "ERROR: no hard disk found.", COL_RED, BG);
                gfx_flip();
                for (;;) __asm__ volatile("pause");
            }

            // 在镜像副本里补用户库与主机名
            uint8_t* img = (uint8_t*)(uintptr_t)install_image;
            if (host[0]) cpy(img + HOST_OFF, host, 15);
            img[SB_OFF + 0] = 'U'; img[SB_OFF + 1] = 'S';
            img[SB_OFF + 2] = 'R'; img[SB_OFF + 3] = '1';
            img[SB_OFF + 4] = guest_chk.checked ? 2 : 1;
            set_rec(img + REC_OFF, 0, ROOT_ROLE, admin, adminpw);
            if (guest_chk.checked) {
                set_rec(img + REC_OFF, 1, GUEST_ROLE, "guest", "guest");
            }

            int total = (int)(install_image_size / SECTOR);
            // 容量自检: 镜像扇区数不得超过磁盘容量, 且安装标记扇区必须在盘内。
            // 否则 ata_write_sectors 会越界回绕写坏数据。
            uint64_t disk_total = ata_total_sectors();
            if ((uint64_t)total > disk_total || (uint64_t)MARKER_LBA >= disk_total) {
                text(40, 150, "ERROR: image does not fit on disk.", COL_RED, BG);
                gfx_flip();
                for (;;) __asm__ volatile("pause");
            }
            int chunk = 64;
            for (int lba = 0; lba < total; lba += chunk) {
                int nn = (total - lba < chunk) ? (total - lba) : chunk;
                if (ata_write_sectors((uint32_t)lba, (uint8_t)nn,
                                     img + (uintptr_t)lba * SECTOR) != 0) {
                    text(40, 150, "ERROR: disk write failed.", COL_RED, BG);
                    gfx_flip();
                    for (;;) __asm__ volatile("pause");
                }
                progress((lba + nn) * 100 / total);
            }

            // 安装标记
            uint8_t marker[SECTOR];
            for (int i = 0; i < SECTOR; i++) marker[i] = 0;
            marker[0] = 'P'; marker[1] = 'S'; marker[2] = 'B'; marker[3] = '1';
            ata_write_sectors(MARKER_LBA, 1, marker);
            progress(100);

            // ---- 完成页 ----
            fill(0, 0, SW - 1, SH - 1, BG);
            title();
            text(70, 70, "Installation complete!", COL_LGREEN, BG);
            text(40, 90, "Remove the install media and press ENTER to reboot.", COL_WHITE, BG);
            gfx_flip();
            for (;;) {
                int k = kb_poll();
                if (k == KEY_ENTER || k == 27) reboot();
                __asm__ volatile("pause");
            }
        } else {
            page = 0;
        }
    }
}
