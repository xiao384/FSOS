// terminal.c - 系统终端 (Shell), 默认 root(最高)权限
// 纯内核态实现: 滚动文本缓冲 + 行编辑 + 命令历史 + 命令分发
// VBE 640x480, 8x8 字体 => 80x60 字符, 文本区 80x58 (顶部 14px 标题栏)
#include "terminal.h"
#include "vga.h"
#include "theme.h"      // hires: TITLE_H, THEME_SF
#include "kb.h"
#include "user.h"
#include "io.h"
#include "idt.h"
#include "perm.h"       // 权限层: 终端以 ROOT 身份运行
#include "sysconf.h"    // 持久化配置 (root 可改)
#include "mp_entry.h"   // mp_fsos_run / MP_MODE_*
#include "driver.h"
#include "mouse.h"     // 系统驱动表 (命令 'drivers' 展示)
#include "pinyin.h"    // 拼音输入法
#include "cjk.h"       // CJK 文本渲染 (拼音候选显示)
#include "vm.h"        // 简易虚拟机 (沙盒执行)

#define TERM_COLS        80
#define TERM_ROWS        58
#define TERM_HIST        400
#define TERM_INPUT_MAX   120
#define TERM_CMD_HIST    32

#define PROMPT "root@pxs:~# "

// ---- 滚动文本缓冲 (每行一个颜色) ----
static char    g_lines[TERM_HIST][TERM_COLS + 1];
static uint8_t g_col[TERM_HIST];
static int     g_nlines = 0;

// ---- 当前输入行 ----
static char g_input[TERM_INPUT_MAX + 1];
static int  g_inlen = 0;

// ---- 命令历史 ----
static char g_cmdhist[TERM_CMD_HIST][TERM_INPUT_MAX + 1];
static int  g_hist_cnt = 0;
static int  g_hist_pos = 0;
static char g_hist_scratch[TERM_INPUT_MAX + 1];

// 活动输入行: -1 = 底部输入行; >=0 = 返回页中被点击的屏幕文本行 (覆盖式输入)
static int g_edit_row = -1;

// ============================================================
// 基础字符串工具 (无 libc)
// ============================================================
static int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int str_cmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return *a - *b;
        a++; b++;
    }
    return *a - *b;
}

static void str_cpy(char* d, const char* s) {
    while (*s) *d++ = *s++;
    *d = '\0';
}

static void u32_to_dec(uint32_t v, char* out) {
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = '\0';
}

static void u32_to_hex(uint32_t v, char* out) {
    const char* d = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; i++) { out[9 - i] = d[v & 0xF]; v >>= 4; }
    out[10] = '\0';
}

// ============================================================
// 文本缓冲操作
// ============================================================
static void buf_newline(void) {
    if (g_nlines < TERM_HIST) {
        g_lines[g_nlines][0] = '\0';
        g_col[g_nlines] = COL_LGRAY;
        g_nlines++;
    } else {
        for (int i = 0; i < TERM_HIST - 1; i++) {
            str_cpy(g_lines[i], g_lines[i + 1]);
            g_col[i] = g_col[i + 1];
        }
        g_lines[TERM_HIST - 1][0] = '\0';
        g_col[TERM_HIST - 1] = COL_LGRAY;
    }
}

// 向最后一行追加文本, 必要时自动换行; '\n' 强制换行
static void buf_write(const char* s, uint8_t color) {
    if (g_nlines == 0) buf_newline();
    while (*s) {
        if (*s == '\n') { buf_newline(); s++; continue; }
        char* line = g_lines[g_nlines - 1];
        int pos = str_len(line);
        if (pos >= TERM_COLS) { buf_newline(); line = g_lines[g_nlines - 1]; pos = 0; }
        line[pos++] = *s++;
        line[pos] = '\0';
        g_col[g_nlines - 1] = color;
    }
}

// 写入一行并换行
static void buf_line(const char* s, uint8_t color) {
    buf_write(s, color);
    buf_newline();
}

// 两个字符串拼成一行输出
static void line2(const char* a, const char* b, uint8_t color) {
    char tmp[TERM_COLS + 1];
    int i = 0;
    while (a[i] && i < TERM_COLS) { tmp[i] = a[i]; i++; }
    int j = 0;
    while (b[j] && i < TERM_COLS) { tmp[i++] = b[j++]; }
    tmp[i] = '\0';
    buf_line(tmp, color);
}

// ============================================================
// 屏幕绘制
// ============================================================
// 在指定文本行 (0-based, 0=返回页首行, TERM_ROWS-1=底部输入行) 绘制提示符+输入+光标
static void draw_input_row(int row) {
    int iy = 14 + row * 8;
    int pw = vga_text_w(PROMPT);
    vga_draw_text(2, iy, PROMPT, COL_LGREEN, COL_BLACK);
    int vis = TERM_COLS - pw / 8;
    const char* show = g_input;
    int showlen = g_inlen;
    if (showlen > vis) { show = g_input + (showlen - vis); showlen = vis; }
    if (showlen > 0) vga_draw_text(2 + pw, iy, show, COL_WHITE, COL_BLACK);
    int cx = 2 + pw + showlen * 8;
    vga_fill_rect(cx, iy, cx + 5, iy + 7, COL_WHITE);
}

static void term_draw(void) {
    vga_clear(COL_BLACK);

    // 顶部标题栏 (hires: 高度随 THEME_SF 缩放)
    int th = 13 * THEME_SF;   // 320x200: 13, 1920x1080: 26
    vga_fill_rect(0, 0, VGA_W - 1, th, COL_TITLEBG);
    vga_draw_rect(0, 0, VGA_W - 1, th, COL_LBLUE);
    vga_draw_text(4, 3, "FSOS Terminal", COL_WHITE, COL_TITLEBG);
    const char* right = "root (admin)";
    int rw = vga_text_w(right);
    vga_draw_text(VGA_W - 4 - rw, 3, right, COL_LGREEN, COL_TITLEBG);

    // 滚动文本区 (前 TERM_ROWS-1 行). g_edit_row>=0 时该屏幕行改为活动输入行.
    int start = g_nlines - (TERM_ROWS - 1);
    if (start < 0) start = 0;
    for (int i = 0; i < TERM_ROWS - 1; i++) {
        if (i == g_edit_row) { draw_input_row(i); continue; }  // 点击位置作为活动输入行
        int idx = start + i;
        if (idx >= g_nlines) break;
        vga_draw_text(2, 14 + i * 8, g_lines[idx], g_col[idx], COL_BLACK);
    }

    // 最底行为默认输入行; 当点击某返回页行作为活动输入行(g_edit_row>=0)时,
    // 底部不再重复绘制输入行, 仅保留被点击行作为唯一输入位置, 避免双输入行歧义
    if (g_edit_row < 0) {
        draw_input_row(TERM_ROWS - 1);
    }

    // 拼音输入法状态显示
    if (pinyin_active()) {
        // 在标题栏右侧显示 [中]
        vga_draw_text(VGA_W - 40, 3, "[中]", COL_YELLOW, COL_TITLEBG);
        // 候选列表显示在倒数第二行
        const char* cand = pinyin_get_candidates();
        const char* compose = pinyin_get_compose();
        if (compose[0]) {
            // 16px 高的输入法提示带 (汉字 16px, 条带向上抬 8px 以容纳)
            int cy = 14 + (TERM_ROWS - 2) * 8 - 8;
            vga_fill_rect(0, cy, VGA_W - 1, cy + 15, COL_DGRAY);
            char buf[16];
            buf[0] = '[';
            int p = 1;
            for (int i = 0; compose[i] && p < 14; i++) buf[p++] = compose[i];
            buf[p++] = ']';
            buf[p] = 0;
            vga_draw_text(2, cy + 4, buf, COL_YELLOW, COL_DGRAY);
            if (cand[0]) {
                cjk_text(2 + 80, cy, cand, COL_WHITE, COL_DGRAY);
            }
        }
    }

    // 鼠标指针 (提供点击反馈)
    mouse_state_t m;
    mouse_get(&m);
    if (m.present) {
        vga_fill_rect(m.x, m.y, m.x + 5, m.y + 7, COL_WHITE);
    }
}

// ============================================================
// 命令实现
// ============================================================
static char* next_tok(char** p) {
    char* s = *p;
    while (*s == ' ') s++;
    if (*s == '\0') { *p = s; return 0; }
    char* t = s;
    while (*s && *s != ' ') s++;
    if (*s) { *s = '\0'; *p = s + 1; }
    else { *p = s; }
    return t;
}

// ============================================================
// 命令表 (表驱动; 支持 root 运行时注册自定义命令)
// ============================================================
typedef void (*cmd_fn)(char* args);
typedef struct {
    const char* name;
    cmd_fn      fn;
    const char* help;
    int         dynamic;    // 1 = root 动态注册 (非核心)
    const char* macro;      // 非 NULL => 运行时执行该命令字符串 (用户宏)
} cmd_entry_t;

#define CMD_MAX 64
#define MACRO_MAX 16
static cmd_entry_t g_cmds[CMD_MAX];
static int         g_ncmds = 0;
static cmd_entry_t* g_cur_macro = 0;                       // 当前宏命令入口
static char        g_macro_lines[MACRO_MAX][TERM_INPUT_MAX + 1];
static int         g_macro_cnt = 0;

// 前向声明
static void show_help(void);
static void term_init_cmds(void);
static void sys_reboot(void);
static void sys_poweroff(void);

// 特殊命令包装 (统一为 cmd_fn 签名, 忽略参数)
static void cmd_help(char* a)     { (void)a; show_help(); }
static void cmd_clear(char* a)    { (void)a; g_nlines = 0; }
static void cmd_whoami(char* a)   { (void)a; buf_line("root", COL_WHITE); }
static void cmd_python(char* a)   { (void)a; g_inlen = 0; g_input[0] = '\0'; mp_fsos_run(MP_MODE_REPL); }
static void cmd_bt(char* a)       { (void)a; g_inlen = 0; g_input[0] = '\0'; mp_fsos_run(MP_MODE_BT); }
static void cmd_reboot(char* a)   { (void)a; buf_line("rebooting...", COL_YELLOW); sys_reboot(); }
static void cmd_poweroff(char* a) { (void)a; buf_line("powering off...", COL_YELLOW); sys_poweroff(); }

// 用户宏命令: 运行时执行绑定的命令字符串 (root 自定义命令)
static void cmd_macro(char* args) {
    (void)args;
    if (g_cur_macro && g_cur_macro->macro) run_command(g_cur_macro->macro);
}

static int cmd_add(const char* name, cmd_fn fn, const char* help, int dynamic, const char* macro) {
    if (g_ncmds >= CMD_MAX) return -1;
    g_cmds[g_ncmds].name = name;
    g_cmds[g_ncmds].fn = fn;
    g_cmds[g_ncmds].help = help;
    g_cmds[g_ncmds].dynamic = dynamic;
    g_cmds[g_ncmds].macro = macro;
    g_ncmds++;
    return 0;
}

// root 运行时注册自定义命令 (name + 要执行的命令字符串, 即宏)
int term_register_cmd(const char* name, const char* line) {
    if (!name || !line) return -1;
    for (int i = 0; i < g_ncmds; i++)
        if (str_cmp(g_cmds[i].name, name) == 0) return -1;  // 重名拒绝
    if (g_macro_cnt >= MACRO_MAX) return -1;
    int slot = g_macro_cnt++;
    int j = 0;
    while (line[j] && j < TERM_INPUT_MAX) { g_macro_lines[slot][j] = line[j]; j++; }
    g_macro_lines[slot][j] = '\0';
    return cmd_add(name, cmd_macro, "(user macro)", 1, g_macro_lines[slot]);
}

static void show_help(void) {
    buf_line("Available commands (root):", COL_YELLOW);
    for (int i = 0; i < g_ncmds; i++) {
        if (!g_cmds[i].help || !g_cmds[i].help[0]) continue;
        char tmp[TERM_COLS + 1];
        int j = 0;
        tmp[j++] = ' '; tmp[j++] = ' ';
        for (int c = 0; g_cmds[i].name[c] && j < 16; c++) tmp[j++] = g_cmds[i].name[c];
        for (; j < 16; j++) tmp[j++] = ' ';
        for (int c = 0; g_cmds[i].help[c] && j < TERM_COLS; c++) tmp[j++] = g_cmds[i].help[c];
        tmp[j] = '\0';
        buf_line(tmp, g_cmds[i].dynamic ? COL_LGREEN : COL_LGRAY);
    }
}

static void cmd_echo(char* args) {
    while (*args == ' ') args++;
    buf_line(args, COL_WHITE);
}

extern char __kernel_end[];

static void cmd_meminfo(char* args) { (void)args;
    char tmp[16];
    uint32_t size = (uint32_t)((uintptr_t)__kernel_end - 0x100000u);
    u32_to_hex((uint32_t)(uintptr_t)__kernel_end, tmp);
    line2("Kernel end : ", tmp, COL_WHITE);
    u32_to_dec(size / 1024, tmp);
    line2("Kernel size: ", tmp, COL_WHITE);
    buf_line("Base 0x00100000  FB 0xA0000 (64KB)", COL_LGRAY);
}

static void cmd_uptime(char* args) { (void)args;
    uint32_t ms = get_ticks();
    char tmp[16];
    u32_to_dec(ms / 1000u, tmp);
    line2("Uptime     : ", tmp, COL_WHITE);
    u32_to_dec(ms % 1000u, tmp);
    line2("  millis   : ", tmp, COL_WHITE);
    buf_line("  (PIT timer, 1000 Hz)", COL_LGRAY);
}

static void cmd_diskinfo(char* args) { (void)args;
    uint16_t id[256];
    // ATA IDENTIFY (master, PIO)
    outb(0x1F6, 0xA0);
    outb(0x1F1, 0); outb(0x1F2, 0); outb(0x1F3, 0);
    outb(0x1F4, 0); outb(0x1F5, 0);
    outb(0x1F7, 0xEC);
    int ok = -1;
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(0x1F7);
        if (st & 0x80) continue;          // BSY
        if (st & 0x01) break;             // ERR
        if (st & 0x08) {                  // DRQ
            for (int j = 0; j < 256; j++) id[j] = inw(0x1F0);
            ok = 0;
            break;
        }
    }
    if (ok != 0) {
        buf_line("No ATA drive responded", COL_LRED);
        return;
    }
    char model[41];
    for (int i = 0; i < 20; i++) {
        model[i * 2]     = (char)(id[27 + i] >> 8);
        model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    model[40] = '\0';
    int e = 39;
    while (e > 0 && model[e] == ' ') { model[e] = '\0'; e--; }
    uint32_t sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    char tmp[16];
    buf_write("Model : ", COL_LGRAY);
    buf_write(model, COL_WHITE);
    buf_newline();
    buf_write("Size  : ", COL_LGRAY);
    u32_to_dec(sectors / 2048, tmp);
    buf_write(tmp, COL_WHITE);
    buf_write(" MB (", COL_LGRAY);
    u32_to_dec(sectors, tmp);
    buf_write(tmp, COL_WHITE);
    buf_write(" sectors)", COL_LGRAY);
    buf_newline();
}

static void cmd_users(char* args) { (void)args;
    char tmp[16];
    char line[TERM_COLS + 1];
    int cnt = user_count();
    buf_write("Total users: ", COL_LGRAY);
    u32_to_dec((uint32_t)cnt, tmp);
    buf_write(tmp, COL_WHITE);
    buf_newline();
    for (int i = 0; i < cnt; i++) {
        const UserRec* u = user_get(i);
        if (!u) break;
        int j = 0;
        if (i < 10) line[j++] = (char)('0' + i);
        line[j++] = ' ';
        for (int c = 0; u->name[c] && j < TERM_COLS - 9; c++) line[j++] = u->name[c];
        for (; j < TERM_COLS - 9; j++) line[j] = ' ';
        const char* r = (u->role == ROLE_ADMIN) ? "[admin]" : "[user]";
        for (int c = 0; r[c] && j < TERM_COLS; c++) line[j++] = r[c];
        line[j] = '\0';
        buf_line(line, (u->role == ROLE_ADMIN) ? COL_YELLOW : COL_WHITE);
    }
}

static void cmd_useradd(char* args) {
    char* p = args;
    char* name = next_tok(&p);
    char* pass = next_tok(&p);
    char* role = next_tok(&p);
    if (!name || !pass) {
        buf_line("usage: useradd <name> <pass> [1=admin]", COL_LRED);
        return;
    }
    uint8_t r = (role && role[0] == '1') ? ROLE_ADMIN : ROLE_NORMAL;
    int rc = user_add(name, pass, r);
    if (rc == 0) {
        user_save();
        buf_line("user added", COL_LGREEN);
    } else if (rc == -2) {
        buf_line("ERROR: name already exists", COL_LRED);
    } else {
        buf_line("ERROR: cannot add user", COL_LRED);
    }
}

static void cmd_userdel(char* args) {
    char* p = args;
    char* name = next_tok(&p);
    if (!name) {
        buf_line("usage: userdel <name>", COL_LRED);
        return;
    }
    int idx = user_find(name);
    if (idx < 0) {
        buf_line("ERROR: user not found", COL_LRED);
        return;
    }
    int rc = user_remove(idx);
    if (rc == 0) {
        user_save();
        buf_line("user deleted", COL_LGREEN);
    } else if (rc == -2) {
        buf_line("ERROR: cannot delete last admin", COL_LRED);
    } else {
        buf_line("ERROR: cannot delete", COL_LRED);
    }
}

static void cmd_passwd(char* args) {
    char* p = args;
    char* name = next_tok(&p);
    char* pass = next_tok(&p);
    if (!name || !pass) {
        buf_line("usage: passwd <name> <pass>", COL_LRED);
        return;
    }
    int idx = user_find(name);
    if (idx < 0) {
        buf_line("ERROR: user not found", COL_LRED);
        return;
    }
    user_setpass(idx, pass);
    user_save();
    buf_line("password changed", COL_LGREEN);
}

static void cmd_setrole(char* args) {
    char* p = args;
    char* name = next_tok(&p);
    char* role = next_tok(&p);
    if (!name || !role) {
        buf_line("usage: setrole <name> <0|1>", COL_LRED);
        return;
    }
    int idx = user_find(name);
    if (idx < 0) {
        buf_line("ERROR: user not found", COL_LRED);
        return;
    }
    uint8_t r = (role[0] == '1') ? ROLE_ADMIN : ROLE_NORMAL;
    // 不允许移除最后一个管理员
    if (r == ROLE_NORMAL && user_get(idx)->role == ROLE_ADMIN) {
        int admins = 0;
        for (int i = 0; i < user_count(); i++)
            if (user_get(i)->role == ROLE_ADMIN) admins++;
        if (admins <= 1) {
            buf_line("ERROR: last admin cannot be demoted", COL_LRED);
            return;
        }
    }
    user_setrole(idx, r);
    user_save();
    buf_line("role updated", COL_LGREEN);
}

static void cmd_theme(char* args) {
    if (!perm_can_modify()) { buf_line("permission denied (need root/admin)", COL_LRED); return; }
    char* p = args;
    char* tok = next_tok(&p);
    if (!tok || tok[0] < '0' || tok[0] > '2' || tok[1] != '\0') {
        buf_line("usage: theme 0|1|2   (0=blue 1=dark 2=green)", COL_LRED);
        return;
    }
    sysconf()->theme = (uint8_t)(tok[0] - '0');
    if (sysconf_save() == 0) {
        char msg[32];
        const char* names[3] = {"blue", "dark", "green"};
        // 安全拼接
        int i = 0;
        const char* pre = "theme set to ";
        while (pre[i]) { msg[i] = pre[i]; i++; }
        int j = 0;
        while (names[sysconf()->theme][j] && i < 30) { msg[i++] = names[sysconf()->theme][j++]; }
        msg[i] = '\0';
        buf_line(msg, COL_LGREEN);
        buf_line("(applies on next desktop launch; core untouched)", COL_LGRAY);
    } else {
        buf_line("ERROR: cannot save config", COL_LRED);
    }
}

static void cmd_config(char* args) { (void)args;
    sysconf_t* c = sysconf();
    buf_write("hostname : ", COL_LGRAY);
    buf_write(c->hostname, COL_WHITE);
    buf_newline();
    buf_write("theme    : ", COL_LGRAY);
    const char* names[3] = {"blue", "dark", "green"};
    uint8_t t = c->theme < 3 ? c->theme : 0;
    buf_write(names[t], COL_WHITE);
    buf_newline();
    buf_write("wallpaper: COL ", COL_LGRAY);
    char tmp[8]; u32_to_dec(c->wallpaper, tmp);
    buf_write(tmp, COL_WHITE);
    buf_newline();
    buf_line("core: read-only (kernel region protected)", COL_ORANGE);
}

static void cmd_ver(char* args) { (void)args;
    buf_line("FSOS (Freedom Security OS) v0.2  (i686 kernel)", COL_WHITE);
    buf_line("Terminal 1.0  root shell", COL_LGREEN);
}

// 列出全部系统驱动及其状态 (root 可经 krn.driver_load() 加载动态驱动)
static void cmd_drivers(char* args) { (void)args;
    char line[TERM_COLS + 1];
    int n = drv_count();
    buf_write("Drivers (", COL_LGRAY);
    char tmp[8]; u32_to_dec((uint32_t)n, tmp);
    buf_write(tmp, COL_WHITE);
    buf_write("):", COL_LGRAY);
    buf_newline();
    for (int i = 0; i < n; i++) {
        driver_t* d = drv_get(i);
        if (!d) break;
        int j = 0;
        for (int c = 0; d->name[c] && j < 10; c++) line[j++] = d->name[c];
        for (; j < 10; j++) line[j] = ' ';
        const char* t = drv_type_str(d->type);
        for (int c = 0; t[c] && j < 18; c++) line[j++] = t[c];
        for (; j < 18; j++) line[j] = ' ';
        const char* st = drv_status_str(d->status);
        for (int c = 0; st[c] && j < 26; c++) line[j++] = st[c];
        for (; j < 26; j++) line[j] = ' ';
        for (int c = 0; d->desc[c] && j < TERM_COLS; c++) line[j++] = d->desc[c];
        line[j] = '\0';
        buf_line(line, (d->status > 0) ? COL_LGREEN
                    : (d->status < 0) ? COL_LRED : COL_LGRAY);
    }
    buf_line("core drivers are read-only; root can register extras.", COL_ORANGE);
}

// ---- 系统电源 ----
static void sys_reboot(void) {
    vga_clear(COL_BLACK);
    vga_draw_text_center(90, "Rebooting...", COL_WHITE, COL_BLACK);
    for (volatile int i = 0; i < 2000000; i++) { }
    outb(0x64, 0xFE);            // 8042 键盘控制器复位
    for (;;) __asm__ volatile("cli; hlt");
}

static void sys_poweroff(void) {
    vga_clear(COL_BLACK);
    vga_draw_text_center(90, "Power off...", COL_WHITE, COL_BLACK);
    for (volatile int i = 0; i < 2000000; i++) { }
    outw(0x604, 0x2000);         // QEMU ACPI (ICH9)
    outw(0xB004, 0x2000);        // BOCHS / QEMU debug exit
    outw(0x4004, 0x3400);        // QEMU ACPI (PIIX4)
    for (;;) __asm__ volatile("cli; hlt");
}

// 注册全部内置命令 (放在所有 cmd_* 定义之后, 以便引用其函数指针)
static void term_init_cmds(void) {
    g_ncmds = 0;
    cmd_add("help",     cmd_help,     "show this help", 0, 0);
    cmd_add("echo",     cmd_echo,     "echo <text>", 0, 0);
    cmd_add("clear",    cmd_clear,    "clear screen", 0, 0);
    cmd_add("cls",      cmd_clear,    "clear screen", 0, 0);
    cmd_add("whoami",   cmd_whoami,   "show current user", 0, 0);
    cmd_add("id",       cmd_whoami,   "show current user", 0, 0);
    cmd_add("theme",    cmd_theme,    "theme 0|1|2 (root editable)", 0, 0);
    cmd_add("config",   cmd_config,   "show system config", 0, 0);
    cmd_add("ver",      cmd_ver,      "kernel & terminal info", 0, 0);
    cmd_add("version",  cmd_ver,      "kernel & terminal info", 0, 0);
    cmd_add("uptime",   cmd_uptime,   "time since boot", 0, 0);
    cmd_add("meminfo",  cmd_meminfo,  "kernel memory info", 0, 0);
    cmd_add("diskinfo", cmd_diskinfo, "ATA disk info", 0, 0);
    cmd_add("users",    cmd_users,    "list user accounts", 0, 0);
    cmd_add("useradd",  cmd_useradd,  "useradd <n> <p> [1=admin]", 0, 0);
    cmd_add("userdel",  cmd_userdel,  "userdel <name>", 0, 0);
    cmd_add("passwd",   cmd_passwd,   "passwd <n> <p>", 0, 0);
    cmd_add("setrole",  cmd_setrole,  "setrole <n> <0|1>", 0, 0);
    cmd_add("drivers",  cmd_drivers,  "list system drivers", 0, 0);
    cmd_add("python",   cmd_python,   "enter MicroPython REPL", 0, 0);
    cmd_add("bt",       cmd_bt,       "Better terminal app", 0, 0);
    cmd_add("vm",       vm_command,   "run virtual machine demo", 0, 0);
    cmd_add("reboot",   cmd_reboot,   "restart system", 0, 0);
    cmd_add("poweroff", cmd_poweroff, "power off system", 0, 0);
}

// ============================================================
// 命令分发
// ============================================================
void run_command(const char* line) {
    char buf[TERM_INPUT_MAX + 1];
    str_cpy(buf, line);
    char* p = buf;
    char* cmd = next_tok(&p);
    if (!cmd || *cmd == '\0') return;

    for (int i = 0; i < g_ncmds; i++) {
        if (str_cmp(g_cmds[i].name, cmd) == 0) {
            g_cur_macro = g_cmds[i].macro ? &g_cmds[i] : 0;
            g_cmds[i].fn(p);
            return;
        }
    }
    buf_write("unknown command: ", COL_LRED);
    buf_write(cmd, COL_LRED);
    buf_newline();
    buf_line("type 'help' for commands", COL_LGRAY);
}

int         term_cmd_count(void)      { return g_ncmds; }
const char* term_cmd_name(int i)      { return (i >= 0 && i < g_ncmds) ? g_cmds[i].name : 0; }

// 记录历史 (去重相邻)
static void hist_add(const char* cmd) {
    if (g_hist_cnt > 0 && str_cmp(g_cmdhist[g_hist_cnt - 1], cmd) == 0) return;
    if (g_hist_cnt < TERM_CMD_HIST) {
        str_cpy(g_cmdhist[g_hist_cnt], cmd);
        g_hist_cnt++;
    } else {
        for (int i = 0; i < TERM_CMD_HIST - 1; i++) str_cpy(g_cmdhist[i], g_cmdhist[i + 1]);
        str_cpy(g_cmdhist[TERM_CMD_HIST - 1], cmd);
    }
}

// ============================================================
// 内置 C 终端主循环
// ------------------------------------------------------------
// 这是 FSOS 自带的轻量终端实现。当前系统终端默认走
// Better terminal (见下方 terminal_run), 本函数作为回退路径:
// 当内核未编译 MicroPython 时 (mp_available() == 0), 保证"终端"
// 仍然是一个可用的入口, 而不是只剩一句"功能未启用"的提示。
// ============================================================
static void terminal_run_c(void) {
    g_nlines = 0;
    g_inlen = 0; g_input[0] = '\0';
    g_hist_cnt = 0; g_hist_pos = 0; g_hist_scratch[0] = '\0';
    g_edit_row = -1;     // 默认用底部输入行

    // 终端以 ROOT 身份运行: 可修改除核心以外的所有配置
    perm_set_role(ROLE_ROOT);
    sysconf_load();
    term_init_cmds();   // 构建命令表 (表驱动)

    buf_line("FSOS Terminal", COL_YELLOW);
    buf_line("You are logged in as root (highest privileges).", COL_LGREEN);
    buf_line("Type 'help' for commands, 'exit' to return to desktop.", COL_LGRAY);
    buf_line("F2: toggle Chinese pinyin input.", COL_LGRAY);
    buf_line("Tip: click any line in the output to type a command there.", COL_LGRAY);
    buf_newline();

    static mouse_state_t g_prev_mouse = {0};

    for (;;) {
        term_draw();
        gfx_flip();              // 刷新画面 (终端用 kb_poll 非阻塞, 需手动翻页)

        // 鼠标: 检测左键按下边沿 -> 在返回页点击位置开启活动输入行
        mouse_state_t m;
        mouse_get(&m);
        if (m.left && !g_prev_mouse.left) {
            if (m.y >= 14) {
                int row = (m.y - 14) / 8;
                if (row >= TERM_ROWS - 1) {
                    g_edit_row = -1;        // 点到底部输入行区域 => 用底部输入
                } else {
                    g_edit_row = row;       // 点中返回页某行 => 在该行输入
                    g_inlen = 0; g_input[0] = '\0';
                    g_hist_pos = g_hist_cnt;
                }
            }
        }
        g_prev_mouse = m;

        // 键盘: 非阻塞, 处理所有排队按键 (鼠标点击不阻塞键盘)
        int k;
        while ((k = kb_poll()) != 0) {
            // F2: 切换拼音输入法
            if (k == KEY_F2) {
                pinyin_toggle();
                continue;
            }
            // 拼音模式: 字母/空格/数字/退格/Esc 送入拼音引擎
            if (pinyin_active() && ((k >= 'a' && k <= 'z') || k == ' ' ||
                (k >= '1' && k <= '9') || k == KEY_BS || k == KEY_ESC)) {
                int r = pinyin_feed(k);
                if (r == -1) {
                    // 选出汉字: 插入 UTF-8 到输入缓冲
                    const char* out = pinyin_get_output();
                    for (int i = 0; out[i] && g_inlen < TERM_INPUT_MAX; i++) {
                        g_input[g_inlen++] = out[i];
                    }
                    g_input[g_inlen] = '\0';
                }
                continue;
            }
            if (k >= 32 && k <= 126) {
                if (g_inlen < TERM_INPUT_MAX) {
                    g_input[g_inlen++] = (char)k;
                    g_input[g_inlen] = '\0';
                }
            } else if (k == KEY_BS) {
                if (g_inlen > 0) { g_inlen--; g_input[g_inlen] = '\0'; }
            } else if (k == KEY_ENTER) {
                // 提交提示符行到滚动缓冲
                char full[TERM_INPUT_MAX + 24];
                str_cpy(full, PROMPT);
                str_cpy(full + str_len(PROMPT), g_input);
                buf_line(full, COL_LGREEN);

                g_edit_row = -1;             // 退出活动输入行

                if (g_inlen == 0) continue;
                if (str_cmp(g_input, "exit") == 0) return;

                hist_add(g_input);
                run_command(g_input);
                g_inlen = 0; g_input[0] = '\0';
                g_hist_pos = g_hist_cnt;
            } else if (k == KEY_UP) {
                if (g_hist_cnt > 0) {
                    if (g_hist_pos == g_hist_cnt) str_cpy(g_hist_scratch, g_input);
                    if (g_hist_pos > 0) {
                        g_hist_pos--;
                        str_cpy(g_input, g_cmdhist[g_hist_pos]);
                        g_inlen = str_len(g_input);
                    }
                }
            } else if (k == KEY_DOWN) {
                if (g_hist_pos < g_hist_cnt) {
                    g_hist_pos++;
                    if (g_hist_pos < g_hist_cnt) {
                        str_cpy(g_input, g_cmdhist[g_hist_pos]);
                        g_inlen = str_len(g_input);
                    } else {
                        str_cpy(g_input, g_hist_scratch);
                        g_inlen = str_len(g_input);
                    }
                }
            } else if (k == KEY_ESC) {
                if (g_edit_row >= 0) {       // ESC 优先取消活动输入行
                    g_edit_row = -1;
                    g_inlen = 0; g_input[0] = '\0';
                    g_hist_pos = g_hist_cnt;
                } else {
                    return;                   // 否则退出终端
                }
            } else if (k == 4) {             // Ctrl+D: 退出终端
                return;
            }
        }

        // 无输入时让出 CPU (等待键盘/鼠标中断唤醒)
        __asm__ volatile("sti; hlt");
        g_idle_ticks++;   // 计入空闲统计
    }
}

// ============================================================
// 系统终端统一入口
// ------------------------------------------------------------
// 系统终端 = Better terminal (MicroPython 应用 pyroot/bt.py)。
// 它具备完整的命令集 (help/message/users/pkg/install/run ...) 且支持
// 点击返回页任意行就地输入命令、回车执行。
//
// 健壮性: 若 MicroPython 未编译, 或 Better terminal 字节码编译/运行失败,
// mp_fsos_run 返回 0, 此时回退到内置 C 终端 —— 保证"终端"永远是可用入口,
// 不会再出现 BT 一闪即退、黑屏回到桌面的现象。
// ============================================================
void terminal_run(void) {
    if (mp_available() && mp_fsos_run(MP_MODE_BT)) {
        return;                            // Better terminal 正常运行 (退出后返回桌面)
    }
    terminal_run_c();                      // 回退: 内置 C 终端
}
