// devstudio.c - FSOS 集成开发入口
//
// 三种模式:
//   M_LIST    文件列表 (新建/打开/删除/直接运行)
//   M_EDIT    文本编辑 (上下左右移动、回车换行、退格/删除、Ctrl+S 保存、Ctrl+R 运行)
//   M_NEWNAME 新建文件名输入
//
// 文件落在内核文件区 (filesys.h): 最多 16 个、每个 <= 4KB、纯文本, 重启后仍在。
#include "devstudio.h"
#include "filesys.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "lang.h"
#include "kb.h"
#include "sysconf.h"
#include "vscode.h"
#include <stdint.h>

#define DEV_MAX_FILES 16
#define DEV_BUF       3900      // 编辑缓冲 (略小于 4KB, 留出 NUL)
#define DEV_COLS      36        // 可见列数 (8px ASCII)
#define DEV_ROWS      11        // 可见行数
#define DEV_NAME      24

typedef enum { M_LIST = 0, M_EDIT, M_NEWNAME } dev_mode_t;

static char g_names[DEV_MAX_FILES][DEV_NAME];
static int  g_nfiles;
static int  g_sel;                       // 列表选中项
static int  g_mode;                      // 当前模式
static char g_cur[DEV_NAME];             // 当前文件名
static char g_buf[DEV_BUF];              // 编辑缓冲
static int  g_len;                       // 缓冲长度
static int  g_pos;                       // 光标 (buf 下标)
static int  g_top;                       // 视口首行
static int  g_dirty;                     // 未保存标记
static char g_msg[48];                   // 状态提示
static char g_new[DEV_NAME];             // 新建文件名输入
static int  g_newlen;
static int  g_skip;                      // 运行后请求跳过鼠标边沿

// ---------------- 小工具 ----------------
static int d_strlen(const char* s) { int n = 0; while (s && s[n]) n++; return n; }

static void d_ncpy(char* d, const char* s, int n) {
    int i = 0;
    for (; i < n - 1 && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
}

static int d_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

// 取扩展名 (指向最后一个 '.'; 无则返回空串指针)
static const char* ext_of(const char* name) {
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;
    return dot ? dot : "";
}

static const char* lang_of(const char* name) {
    const char* e = ext_of(name);
    if (e[0] == 0) return 0;
    if (d_lower(e[1]) == 'p' && d_lower(e[2]) == 'y' && e[3] == 0) return "Python";
    if (d_lower(e[1]) == 'c' && e[2] == 0) return "C/C++";
    if (d_lower(e[1]) == 'c' && d_lower(e[2]) == 'c' && e[3] == 0) return "C/C++";
    if (d_lower(e[1]) == 'c' && d_lower(e[2]) == 'p' && d_lower(e[3]) == 'p' && e[4] == 0) return "C/C++";
    if (d_lower(e[1]) == 'j' && d_lower(e[2]) == 'a' && d_lower(e[3]) == 'v' && d_lower(e[4]) == 'a' && e[5] == 0)
        return "Java";
    return 0;
}

// 默认模板: 新建文件时给一份能直接跑的示例
static const char* templ_for(const char* name) {
    switch (lang_of(name) ? lang_of(name)[0] : 0) {
        case 'P': return "print('hello from FSOS')\nfor i in range(3):\n    print(i)\n";
        case 'C': return "// FSOS cint: 整数语义\nint n = 10;\nint s = 0;\nwhile (n > 0) { s = s + n; n = n - 1; }\nprint(s);\n";
        case 'J': return "// FSOS 最小 JVM 演示\nclass Main {\n  main() {\n    print(1);\n  }\n}\n";
        default:  return "\n";
    }
}

// ---------------- 行/列换算 ----------------
static int cur_row(void) {
    int r = 0;
    for (int i = 0; i < g_pos && i < g_len; i++) if (g_buf[i] == '\n') r++;
    return r;
}
static int cur_col(void) {
    int c = 0;
    for (int i = 0; i < g_pos && i < g_len; i++) { if (g_buf[i] == '\n') c = 0; else c++; }
    return c;
}
static int row_start(int row) {
    int r = 0, i = 0;
    if (row <= 0) return 0;
    for (; i < g_len; i++) if (g_buf[i] == '\n') { r++; if (r == row) return i + 1; }
    return g_len;
}
static int row_end(int row) {
    int i = row_start(row);
    while (i < g_len && g_buf[i] != '\n') i++;
    return i;
}
static int row_count(void) {
    int n = 1;
    for (int i = 0; i < g_len; i++) if (g_buf[i] == '\n') n++;
    return n;
}

// ---------------- 编辑操作 ----------------
static void insert_char(char c) {
    if (g_len >= DEV_BUF - 1) return;
    for (int i = g_len; i > g_pos; i--) g_buf[i] = g_buf[i - 1];
    g_buf[g_pos] = c;
    g_len++; g_pos++;
    g_buf[g_len] = 0;
    g_dirty = 1;
}
static void del_back(void) {
    if (g_pos <= 0) return;
    for (int i = g_pos - 1; i < g_len - 1; i++) g_buf[i] = g_buf[i + 1];
    g_len--; g_pos--;
    g_buf[g_len] = 0;
    g_dirty = 1;
}
static void del_fwd(void) {
    if (g_pos >= g_len) return;
    for (int i = g_pos; i < g_len - 1; i++) g_buf[i] = g_buf[i + 1];
    g_len--;
    g_buf[g_len] = 0;
    g_dirty = 1;
}
static void move_up(void) {
    int r = cur_row(), c = cur_col();
    if (r <= 0) { g_pos = 0; return; }
    int rs = row_start(r - 1), re = row_end(r - 1);
    g_pos = rs + ((c < re - rs) ? c : (re - rs));
}
static void move_down(void) {
    int r = cur_row(), c = cur_col();
    int rc = row_count();
    if (r >= rc - 1) { g_pos = g_len; return; }
    int rs = row_start(r + 1), re = row_end(r + 1);
    g_pos = rs + ((c < re - rs) ? c : (re - rs));
}
static void scroll_to_cursor(void) {
    int r = cur_row();
    if (r < g_top) g_top = r;
    else if (r > g_top + DEV_ROWS - 1) g_top = r - DEV_ROWS + 1;
}

// ---------------- 文件操作 ----------------
static void refresh_list(void) {
    fs_init();                                   // 重新读目录扇区 (外部可能写过盘)
    g_nfiles = fs_list(g_names, DEV_MAX_FILES);
    if (g_sel >= g_nfiles) g_sel = g_nfiles - 1;
    if (g_sel < 0) g_sel = 0;
}

void devstudio_open(void) {
    refresh_list();
    if (g_mode == M_LIST) d_ncpy(g_msg, "Ctrl+N 新建  Enter 打开  Ctrl+R 运行", sizeof(g_msg));
}

static void open_file(const char* name) {
    int n = fs_read(name, g_buf, DEV_BUF);
    if (n < 0) { g_len = 0; g_buf[0] = 0; d_ncpy(g_msg, "读取失败", sizeof(g_msg)); }
    else { g_len = n; d_ncpy(g_msg, "已打开", sizeof(g_msg)); }
    d_ncpy(g_cur, name, DEV_NAME);
    g_pos = 0; g_top = 0; g_dirty = 0;
    g_mode = M_EDIT;
}

static void save_file(void) {
    if (!g_cur[0]) { d_ncpy(g_msg, "无文件名", sizeof(g_msg)); return; }
    g_buf[g_len] = 0;
    int rc = fs_write(g_cur, g_buf);
    if (rc == 0) {
        g_dirty = 0;
        // 兼容 Better terminal 的索引文件 (它靠 INDEX.TXT 列目录)
        char idx[512]; int p = 0;
        refresh_list();
        for (int i = 0; i < g_nfiles && p < 480; i++) {
            for (int j = 0; g_names[i][j] && p < 480; j++) idx[p++] = g_names[i][j];
            idx[p++] = '\n';
        }
        idx[p] = 0;
        fs_write("INDEX.TXT", idx);
        d_ncpy(g_msg, "已保存", sizeof(g_msg));
    } else if (rc == -3) {
        d_ncpy(g_msg, "目录已满 (最多 16 个)", sizeof(g_msg));
    } else if (rc == -4) {
        d_ncpy(g_msg, "数据区已满", sizeof(g_msg));
    } else {
        d_ncpy(g_msg, "保存失败", sizeof(g_msg));
    }
}

static void run_file(void) {
    const char* lang = lang_of(g_cur);
    if (!lang) { d_ncpy(g_msg, "未知类型: 需 .py/.c/.java", sizeof(g_msg)); return; }
    if (g_dirty) save_file();
    g_buf[g_len] = 0;
    lang_launch(lang, g_buf, g_cur);          // 全屏运行 (返回后回到桌面)
    g_skip = 1;                               // 忽略紧接着的一次鼠标边沿
    d_ncpy(g_msg, "运行结束", sizeof(g_msg));
}

int devstudio_take_skip(void) { int s = g_skip; g_skip = 0; return s; }

// ---------------- 键盘 ----------------
int devstudio_key(int k) {
    // ---- 新建文件名输入 ----
    if (g_mode == M_NEWNAME) {
        if (k == 13) {
            if (g_newlen > 0) {
                g_new[g_newlen] = 0;
                d_ncpy(g_cur, g_new, DEV_NAME);
                const char* t = templ_for(g_cur);
                int i = 0;
                for (; t[i] && i < DEV_BUF - 1; i++) g_buf[i] = t[i];
                g_buf[i] = 0; g_len = i;
                g_pos = 0; g_top = 0; g_dirty = 1;
                g_mode = M_EDIT;
                d_ncpy(g_msg, "已新建 (Ctrl+S 保存)", sizeof(g_msg));
            } else {
                g_mode = M_LIST;
            }
            return 1;
        }
        if (k == 27) { g_mode = M_LIST; return 1; }
        if (k == 8) { if (g_newlen > 0) g_newlen--; return 1; }
        if (k >= 32 && k <= 126 && g_newlen < DEV_NAME - 1) { g_new[g_newlen++] = (char)k; return 1; }
        return 1;   // 输入态吞掉其余按键
    }

    // ---- 文件列表 ----
    if (g_mode == M_LIST) {
        if (k == KEY_UP)    { if (g_sel > 0) g_sel--; return 1; }
        if (k == KEY_DOWN)  { if (g_sel < g_nfiles - 1) g_sel++; return 1; }
        if (k == 14) {                                   // Ctrl+N 新建
            g_mode = M_NEWNAME; g_newlen = 0; g_new[0] = 0;
            d_ncpy(g_msg, "输入文件名后回车", sizeof(g_msg));
            return 1;
        }
        if (k == 24 && g_nfiles > 0) {                   // Ctrl+X 删除
            fs_remove(g_names[g_sel]);
            refresh_list();
            d_ncpy(g_msg, "已删除", sizeof(g_msg));
            return 1;
        }
        if (k == 86 || k == 118) {                       // V/v 切换到 VSCode 模式
            sysconf_set_dev_app(DEV_APP_VSCODE);
            vscode_open();
            d_ncpy(g_msg, "已切换到 VSCode", sizeof(g_msg));
            return 1;
        }
        if (k == 13 && g_nfiles > 0) { open_file(g_names[g_sel]); return 1; }
        if (k == 18 && g_nfiles > 0) {                   // Ctrl+R 直接运行
            d_ncpy(g_cur, g_names[g_sel], DEV_NAME);
            open_file(g_cur);
            run_file();
            return 1;
        }
        return 0;      // 其余交给全局快捷键 (ESC 退出桌面等)
    }

    // ---- 编辑 ----
    if (k == 19) { save_file(); return 1; }              // Ctrl+S
    if (k == 18) { run_file();  return 1; }              // Ctrl+R
    if (k == 14) {                                       // Ctrl+N 新建
        g_mode = M_NEWNAME; g_newlen = 0; g_new[0] = 0;
        return 1;
    }
    if (k == 27) { refresh_list(); g_mode = M_LIST; return 1; }   // ESC 返回列表 (不退出桌面)
    if (k == 13) { insert_char('\n'); scroll_to_cursor(); return 1; }
    if (k == 8)  { del_back(); scroll_to_cursor(); return 1; }
    if (k == KEY_DEL) { del_fwd(); scroll_to_cursor(); return 1; }
    if (k == KEY_LEFT)  { if (g_pos > 0) g_pos--; scroll_to_cursor(); return 1; }
    if (k == KEY_RIGHT) { if (g_pos < g_len) g_pos++; scroll_to_cursor(); return 1; }
    if (k == KEY_UP)    { move_up();   scroll_to_cursor(); return 1; }
    if (k == KEY_DOWN)  { move_down(); scroll_to_cursor(); return 1; }
    if (k == KEY_HOME)  { g_pos = row_start(cur_row()); scroll_to_cursor(); return 1; }
    if (k == KEY_END)   { g_pos = row_end(cur_row());   scroll_to_cursor(); return 1; }
    if (k == KEY_PGUP)  { g_top -= (DEV_ROWS - 1); if (g_top < 0) g_top = 0; return 1; }
    if (k == KEY_PGDN)  { g_top += (DEV_ROWS - 1); scroll_to_cursor(); return 1; }
    if (k >= 32 && k <= 126) { insert_char((char)k); scroll_to_cursor(); return 1; }
    if (k == 9) { insert_char(' '); insert_char(' '); return 1; }   // Tab -> 两空格
    return 0;
}

// ---------------- 绘制 ----------------
static void draw_row(int x, int y, int row, int is_cur_line) {
    int rs = row_start(row), re = row_end(row);
    char line[DEV_COLS + 1];
    int n = 0;
    for (int i = rs; i < re && n < DEV_COLS; i++) line[n++] = g_buf[i];
    line[n] = 0;
    cjk_text(x, y, line, COL_BLACK, is_cur_line ? COL_ACCENT_SOFT : COL_WHITE);
}

void devstudio_draw(int x, int y, int w, int h) {
    (void)w;
    int cy = y + 15;                                    // 标题栏之下

    // 状态行
    char st[48];
    int p = 0;
    if (g_mode == M_LIST) {
        st[p++] = '['; st[p++] = '0' + (g_nfiles / 10) % 10; st[p++] = '0' + g_nfiles % 10;
        st[p++] = '/'; st[p++] = '1'; st[p++] = '6'; st[p++] = ']'; st[p++] = ' ';
        const char* t = "文件列表";
        for (int i = 0; t[i]; i++) st[p++] = t[i];
        st[p] = 0;
    } else {
        for (int i = 0; g_cur[i] && p < 30; i++) st[p++] = g_cur[i];
        if (g_dirty) { st[p++] = ' '; st[p++] = '*'; }
        st[p] = 0;
    }
    gfx_fill_idx(x, cy, x + w - 1, cy + 11, COL_ACCENT);
    cjk_text(x + 4, cy + 2, st, COL_WHITE, COL_ACCENT);
    cy += 14;

    if (g_mode == M_LIST) {
        if (g_nfiles == 0) {
            cjk_text(x + 4, cy + 4, "暂无文件 (Ctrl+N 新建)", COL_LGRAY, COL_WHITE);
        }
        for (int i = 0; i < g_nfiles && i < DEV_ROWS; i++) {
            int yy = cy + i * 11;
            if (i == g_sel) gfx_fill_idx(x + 2, yy, x + w - 3, yy + 10, COL_ACCENT_SOFT);
            cjk_text(x + 6, yy + 1, g_names[i],
                     (i == g_sel) ? COL_ACCENT : COL_BLACK,
                     (i == g_sel) ? COL_ACCENT_SOFT : COL_WHITE);
        }
        // 提示行
        cjk_text(x + 4, y + h - 13, "Ctrl+N 新建  Enter 打开  Ctrl+X 删除", COL_LGRAY, COL_WHITE);
    } else if (g_mode == M_NEWNAME) {
        cjk_text(x + 4, cy + 2, "文件名 (如 main.py / a.c / T.java):", COL_BLACK, COL_WHITE);
        gfx_fill_idx(x + 4, cy + 18, x + w - 8, cy + 30, COL_WHITE);
        gfx_rect_idx(x + 4, cy + 18, x + w - 8, cy + 30, COL_ACCENT);
        cjk_text(x + 7, cy + 20, g_new, COL_BLACK, COL_WHITE);
        int cw = 8 * (g_newlen + 1);
        if (cw < w - 16) gfx_fill_idx(x + 6 + 8 * g_newlen, cy + 20, x + 7 + 8 * g_newlen, cy + 28, COL_BLACK);
        cjk_text(x + 4, y + h - 13, "回车确认  ESC 取消", COL_LGRAY, COL_WHITE);
    } else {
        // 文本编辑区
        int rc = row_count();
        if (g_top > rc - 1) g_top = rc - 1;
        if (g_top < 0) g_top = 0;
        for (int r = 0; r < DEV_ROWS; r++) {
            int rr = g_top + r;
            int yy = cy + r * 10;
            if (rr < rc) draw_row(x + 2, yy, rr, 0);
        }
        // 光标 (块状)
        int crow = cur_row(), ccol = cur_col();
        if (crow >= g_top && crow < g_top + DEV_ROWS) {
            int cx = x + 2 + ccol * 8;
            int cyy = cy + (crow - g_top) * 10;
            gfx_fill_idx(cx, cyy + 1, cx + 1, cyy + 8, COL_BLACK);
        }
        // 底部: 位置 + 提示
        char pos[16];
        int q = 0, cr = crow + 1, cc = ccol + 1;
        pos[q++] = 'L'; pos[q++] = '0' + (cr / 10) % 10; pos[q++] = '0' + cr % 10;
        pos[q++] = ':';
        pos[q++] = 'C'; pos[q++] = '0' + (cc / 10) % 10; pos[q++] = '0' + cc % 10;
        pos[q] = 0;
        cjk_text(x + w - 60, y + h - 13, pos, COL_LGRAY, COL_WHITE);
        cjk_text(x + 4, y + h - 13, "Ctrl+S 保存  Ctrl+R 运行  ESC 返回", COL_LGRAY, COL_WHITE);
    }

    if (g_msg[0]) cjk_text(x + 150, y + 17, g_msg, COL_YELLOW, COL_ACCENT);
}
