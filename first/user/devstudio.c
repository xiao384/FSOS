// devstudio.c - FSOS 集成开发入口
//
// 三种模式:
//   M_LIST    文件列表 (新建/打开/删除/直接运行)
//   M_EDIT    文本编辑 (上下左右移动、回车换行、退格/删除、Ctrl+S 保存、Ctrl+R 运行)
//   M_NEWNAME 新建文件名输入
//
// 文件落在内核文件区 (filesys.h): 最多 16 个、每个 <= 4KB、纯文本, 重启后仍在。
#include "devstudio.h"
#include "window.h"   // GUI window lifecycle: GUI_KEY_CLOSE
#include "filesys.h"
#include "gfx.h"
#include "vga.h"
#include "theme_api.h"   // gui_framework Phase 10: Theme API 集成点
#include "cjk.h"
#include "lang.h"
#include "module.h"      // console output capture / execution status
#include "kb.h"
#include "idt.h"          // get_ticks() for caret blinking
#include "sysconf.h"
#include "vscode.h"
#include <stdint.h>

#define DEV_MAX_FILES 16
#define DEV_BUF       3900      // 编辑缓冲 (略小于 4KB, 留出 NUL)
#define DEV_COLS      240       // 缓冲显示的最大代码列；实际视口按窗口宽度计算
#define DEV_ROWS      64        // 实际可见行数动态计算，此值仅为键盘分页上限
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
static char g_pref_lang[12];               // 从启动器进入时的语言偏好

// 运行结果与动态布局状态。
static int  g_dx, g_dy, g_dw, g_dh;
static int  g_sidebar_w;
static int  g_code_x, g_code_y, g_code_w, g_code_h;
static int  g_code_row_h, g_code_char_w;
static int  g_view_rows;
static int  g_output_h;
static char g_output[4096];
static int  g_output_len;
static int  g_run_rc;

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
        case 'C': return "// FSOS C/C++ demo\nint main() {\n    int n = 10;\n    int s = 0;\n    while (n > 0) { s += n; --n; }\n    return s;\n}\n";
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
    int rows = g_view_rows > 0 ? g_view_rows : 20;
    if (r < g_top) g_top = r;
    else if (r > g_top + rows - 1) g_top = r - rows + 1;
}

// ---------------- 文件操作 ----------------
static void refresh_list(void) {
    fs_init();                                   // 重新读目录扇区 (外部可能写过盘)
    g_nfiles = fs_list(g_names, DEV_MAX_FILES);
    if (g_sel >= g_nfiles) g_sel = g_nfiles - 1;
    if (g_sel < 0) g_sel = 0;
}

void devstudio_open(void) {
    g_mode = M_LIST;
    g_dirty = 0;
    g_skip = 0;
    g_newlen = 0;
    g_new[0] = 0;
    g_output_len = 0; g_output[0] = 0; g_run_rc = 0;
    refresh_list();
    if (g_mode == M_LIST) d_ncpy(g_msg, "Ctrl+N 新建  Enter 打开  Ctrl+R 编译/运行", sizeof(g_msg));
}

void devstudio_open_language(const char* language) {
    if (!language) { g_pref_lang[0] = 0; devstudio_open(); return; }
    d_ncpy(g_pref_lang, language, sizeof(g_pref_lang));
    devstudio_open();
    // 为语言入口提供最小、可立即运行的模板；不会覆盖已有磁盘文件。
    // 用户仍可在列表中打开已有工程文件。
    if (g_mode == M_LIST && g_nfiles == 0) {
        const char* ext = ".py";
        if (language[0] == 'C') ext = ".c";
        else if (language[0] == 'J') ext = ".java";
        d_ncpy(g_new, (language[0] == 'P') ? "main.py" :
                      (language[0] == 'C') ? "main.c" : "Main.java", DEV_NAME);
        g_newlen = d_strlen(g_new);
        (void)ext;
        // 不自动创建文件，避免用户尚未保存就污染文件系统。
        d_ncpy(g_msg, "Ctrl+N 新建当前语言文件", sizeof(g_msg));
    }
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
        int buf_cap = SCREEN_H * 480 / 200;  // hires: 按屏幕高度缩放缓冲区上限
        for (int i = 0; i < g_nfiles && p < buf_cap; i++) {
            for (int j = 0; g_names[i][j] && p < buf_cap; j++) idx[p++] = g_names[i][j];
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

    // 每次运行都从一个干净的输出会话开始；输出回到 IDE 底部面板。
    console_clear();
    d_ncpy(g_msg, "正在运行...", sizeof(g_msg));
    g_run_rc = lang_launch(lang, g_buf, g_cur);
    int n = console_drain(g_output, (int)sizeof(g_output)-1);
    if (n < 0) n = 0;
    if (n > (int)sizeof(g_output)-1) n = (int)sizeof(g_output)-1;
    g_output[n] = 0;
    g_output_len = n;
    g_skip = 1;
    d_ncpy(g_msg, g_run_rc == 0 ? "运行成功" : "运行失败：请查看底部输出", sizeof(g_msg));
}

void devstudio_close(void) {
    g_mode = M_LIST;
    g_dirty = 0;
    g_len = 0;
    g_pos = 0;
    g_top = 0;
    g_cur[0] = 0;
    g_buf[0] = 0;
    g_newlen = 0;
    g_new[0] = 0;
    g_msg[0] = 0;
    g_skip = 0;
    g_pref_lang[0] = 0;
    g_output_len = 0; g_output[0] = 0; g_run_rc = 0;
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
                // 从语言入口新建时，若用户只输入文件名，自动补上正确扩展名。
                if (!ext_of(g_cur)[0] && g_pref_lang[0]) {
                    const char* ext = (g_pref_lang[0] == 'P') ? ".py" :
                                      (g_pref_lang[0] == 'C') ? ".c" : ".java";
                    int n = d_strlen(g_cur);
                    int en = d_strlen(ext);
                    if (n + en < DEV_NAME) {
                        for (int j = 0; j < en; j++) g_cur[n + j] = ext[j];
                        g_cur[n + en] = 0;
                    }
                }
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
        if (k == 27) {
            return GUI_KEY_CLOSE;       // 文件列表中的 ESC 关闭“开发/编译器”窗口，而不是退出整个桌面
        }
        return 0;      // 其余交给全局快捷键
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
    if (k == KEY_PGUP)  { int step=g_view_rows>1?g_view_rows-1:1; g_top -= step; if (g_top < 0) g_top = 0; return 1; }
    if (k == KEY_PGDN)  { int step=g_view_rows>1?g_view_rows-1:1; g_top += step; scroll_to_cursor(); return 1; }
    if (k >= 32 && k <= 126) { insert_char((char)k); scroll_to_cursor(); return 1; }
    if (k == 9) { insert_char(' '); insert_char(' '); return 1; }   // Tab -> 两空格
    return 0;
}

// ---------------- 绘制 ----------------
static void draw_row(int x, int y, int row, int is_cur_line, int max_chars) {
    int rs = row_start(row), re = row_end(row);
    char line[DEV_COLS + 1];
    int n = 0;
    if (max_chars < 1) max_chars = 1;
    if (max_chars > DEV_COLS) max_chars = DEV_COLS;
    for (int i = rs; i < re && n < max_chars; i++) line[n++] = g_buf[i];
    line[n] = 0;
    cjk_text(x, y, line, COL_LGRAY, is_cur_line ? COL_ACCENT_SOFT : COL_BLACK);
}

static void draw_output_lines(int x, int y, int w, int h) {
    uint8_t panel=theme_get_color_idx(COLOR_BG_PANEL), soft=theme_get_color_idx(COLOR_FG_SOFT);
    (void)panel;
    int sc=gfx_font_scale(); if(sc<1)sc=1; if(sc>2)sc=2;
    int rh=8*sc+5, cw=8*sc; if(cw<8)cw=8;
    int rows=h/rh; if(rows<1)rows=1;
    int cols=w/cw; if(cols<8)cols=8;
    // 找到最后 rows 个换行分隔的视觉行；简单按屏幕列软换行。
    int starts[80],lens[80],nr=0;
    int pos=0;
    while(pos<g_output_len && nr<80){
        int line_start=pos;
        int count=0;
        while(pos<g_output_len && g_output[pos]!='\n' && count<cols){pos++;count++;}
        starts[nr]=line_start; lens[nr]=pos-line_start; nr++;
        if(pos<g_output_len && g_output[pos]=='\n')pos++;
    }
    int first=nr-rows; if(first<0)first=0;
    int yy=y;
    for(int i=first;i<nr;i++){
        char line[256]; int n=lens[i]; if(n>cols)n=cols; if(n>255)n=255;
        for(int j=0;j<n;j++) line[j]=g_output[starts[i]+j];
        line[n]=0;
        uint8_t fg=(g_run_rc==0)?theme_get_color_idx(COLOR_FG):theme_get_color_idx(COLOR_FG_SOFT);
        cjk_text(x,yy,line,fg,theme_get_color_idx(COLOR_BG_PANEL));
        yy+=rh;
        if(yy>y+h-rh)break;
    }
    if(g_output_len==0) cjk_ui_text(x,y,"运行输出会显示在这里",soft,panel);
}

void devstudio_draw(int x, int y, int w, int h) {
    g_dx=x;g_dy=y;g_dw=w;g_dh=h;
    int title_h=46;
    int status_h=30;
    g_output_h=(h>=460)?150:((h>=320)?112:80);
    if(g_output_h>h-title_h-status_h-70) g_output_h=h-title_h-status_h-70;
    if(g_output_h<60) g_output_h=60;
    g_sidebar_w=(w>=900)?240:((w>=600)?210:0);
    g_code_char_w=8;
    g_code_row_h=20;
    if(g_code_char_w<8)g_code_char_w=8;
    if(g_code_row_h<14)g_code_row_h=14;

    uint8_t bg=theme_get_color_idx(COLOR_BG_PANEL), title=theme_get_color_idx(COLOR_BG_TITLE), field=theme_get_color_idx(COLOR_FIELD), border=theme_get_color_idx(COLOR_BORDER), fg=theme_get_color_idx(COLOR_FG), soft=theme_get_color_idx(COLOR_FG_SOFT), accent=theme_get_color_idx(COLOR_ACCENT), hover=theme_get_color_idx(COLOR_HOVER);
    gfx_fill_idx(x,y,x+w-1,y+h-1,bg);

    // Toolbar
    gfx_fill_idx(x,y,x+w-1,y+title_h-1,title);
    cjk_ui_text(x+18,y+8,"开发",fg,title);
    if(g_mode==M_EDIT) cjk_ui_text(x+94,y+8,g_cur,soft,title);
    else if(g_mode==M_NEWNAME) cjk_ui_text(x+94,y+8,"新建文件",soft,title);
    else cjk_ui_text(x+94,y+8,"工程文件",soft,title);
    // Run/Save status chips
    int chipx=x+w-190;
    if(g_run_rc==0 && g_output_len>0) { gfx_fill_idx(chipx,y+8,chipx+74,y+30,theme_get_color_idx(COLOR_SUCCESS)); cjk_ui_text(chipx+12,y+10,"运行成功",fg,theme_get_color_idx(COLOR_SUCCESS)); }
    if(g_run_rc!=0 && g_output_len>0) { gfx_fill_idx(chipx,y+8,chipx+74,y+30,theme_get_color_idx(COLOR_DANGER)); cjk_ui_text(chipx+12,y+10,"运行失败",fg,theme_get_color_idx(COLOR_DANGER)); }
    if(g_dirty) { gfx_fill_idx(x+w-100,y+8,x+w-18,y+30,hover); cjk_ui_text(x+w-84,y+10,"未保存",soft,hover); }

    int body_y=y+title_h;
    int status_y=y+h-status_h;
    int main_h=status_y-body_y-g_output_h-1;
    if(main_h<70) main_h=70;
    g_code_x=x+g_sidebar_w;
    g_code_y=body_y;
    g_code_w=w-g_sidebar_w;
    g_code_h=main_h;
    g_view_rows=g_code_h/g_code_row_h; if(g_view_rows<1)g_view_rows=1; if(g_view_rows>DEV_ROWS)g_view_rows=DEV_ROWS;

    // Sidebar
    if(g_sidebar_w){
        gfx_fill_idx(x,body_y,x+g_sidebar_w-1,status_y-1,theme_get_color_idx(COLOR_BG_MENU));
        cjk_ui_text(x+18,body_y+14,"文件",fg,theme_get_color_idx(COLOR_BG_MENU));
        int ly=body_y+52;
        for(int i=0;i<g_nfiles;i++){
            if(ly+36>status_y-6)break;
            int sel=(g_mode==M_EDIT&&g_cur[0]&&d_lower(g_names[i][0])==d_lower(g_cur[0]));
            // 精确比较文件名
            if(g_mode==M_EDIT){ sel=1; for(int j=0;j<DEV_NAME;j++){ if(g_names[i][j]!=g_cur[j]){sel=0;break;} if(!g_names[i][j])break;} }
            if(i==g_sel && g_mode==M_LIST) sel=1;
            if(sel) gfx_fill_idx(x+10,ly,x+g_sidebar_w-10,ly+30,hover);
            cjk_ui_text_ellipsis(x+22,ly+4,g_names[i],g_sidebar_w-44,sel?fg:soft,sel?hover:theme_get_color_idx(COLOR_BG_MENU));
            ly+=36;
        }
        gfx_line_aa(x+g_sidebar_w-1,body_y,x+g_sidebar_w-1,status_y-1,41,56,74);
    }

    // Editor list mode
    if(g_mode==M_LIST){
        int px=g_code_x+36, py=body_y+42;
        cjk_ui_text(px,py,"选择一个文件开始编辑",fg,bg);
        cjk_ui_text(px,py+40,"Ctrl+N  新建文件",soft,bg);
        cjk_ui_text(px,py+70,"Enter  打开选中文件",soft,bg);
        cjk_ui_text(px,py+100,"Ctrl+R  编译 / 运行",soft,bg);
        cjk_ui_text(px,py+130,"Ctrl+S  保存",soft,bg);
        cjk_ui_text(px,py+190,"语言入口",soft,bg);
        cjk_ui_text(px,py+222,g_pref_lang[0]?g_pref_lang:"Python / C/C++ / Java",accent,bg);
    } else if(g_mode==M_NEWNAME){
        int mw=w-g_sidebar_w-90; if(mw<360)mw=360; if(mw>w)mw=w-40;
        int bx=g_code_x+(g_code_w-mw)/2, by=body_y+62;
        gfx_fill_idx(bx,by,bx+mw-1,by+170,bg); gfx_rect_idx(bx,by,bx+mw-1,by+170,border);
        cjk_ui_text(bx+24,by+22,"新建文件",fg,bg);
        cjk_ui_text(bx+24,by+58,"文件名",soft,bg);
        gfx_fill_idx(bx+20,by+88,bx+mw-20,by+130,field); gfx_rect_idx(bx+20,by+88,bx+mw-20,by+130,accent);
        cjk_ui_text(bx+32,by+98,g_new,fg,field);
        int cw=cjk_ui_text_w(g_new); gfx_fill_idx(bx+32+cw,by+98,bx+33+cw,by+122,fg);
        cjk_ui_text(bx+24,by+144,"Enter 确认    Esc 取消",soft,bg);
    } else {
        int rc=row_count(); if(g_top>rc-1)g_top=rc-1; if(g_top<0)g_top=0;
        int gutter=52; int text_x=g_code_x+gutter+16;
        int max_chars=(g_code_w-gutter-20)/g_code_char_w; if(max_chars<4)max_chars=4; if(max_chars>DEV_COLS)max_chars=DEV_COLS;
        gfx_fill_idx(g_code_x,body_y,g_code_x+g_code_w-1,body_y+main_h-1,bg);
        for(int r=0;r<g_view_rows;r++){
            int rr=g_top+r, yy=body_y+r*g_code_row_h;
            int current=(rr==cur_row());
            if(current)gfx_fill_idx(g_code_x,yy,g_code_x+g_code_w-1,yy+g_code_row_h-1,hover);
            if(rr>=rc)continue;
            char num[12]; int vv=rr+1,n=0; do{num[n++]=(char)('0'+vv%10);vv/=10;}while(vv&&n<10); for(int i=0;i<n/2;i++){char t=num[i];num[i]=num[n-1-i];num[n-1-i]=t;}num[n]=0;
            int nw=cjk_ui_text_w(num); cjk_ui_text(g_code_x+gutter-nw-6,yy+1,num,soft,bg);
            draw_row(text_x,yy,rr,current,max_chars);
        }
        int crow=cur_row(),ccol=cur_col();
        if(crow>=g_top&&crow<g_top+g_view_rows){
            int cx=text_x+ccol*g_code_char_w,cyy=body_y+(crow-g_top)*g_code_row_h;
            if(((get_ticks()/420)&1)==0)gfx_fill_idx(cx,cyy+2,cx+g_code_char_w-1,cyy+g_code_row_h-3,fg);
        }
        // Output panel
        int out_y=body_y+main_h+1;
        gfx_fill_idx(g_code_x,out_y,x+w-1,status_y-1,theme_get_color_idx(COLOR_BG_MENU));
        gfx_line_aa(g_code_x,out_y,x+w-1,out_y,41,56,74);
        cjk_ui_text(g_code_x+18,out_y+8,"输出",fg,theme_get_color_idx(COLOR_BG_MENU));
        draw_output_lines(g_code_x+18,out_y+34,g_code_w-36,(status_y-out_y)-40);
    }

    // status bar
    gfx_fill_idx(x,status_y,x+w-1,y+h-1,accent);
    if(g_mode==M_EDIT){
        char pos[32]; int cr=cur_row()+1,cc=cur_col()+1,p=0; const char*label=(g_msg[0]?g_msg:(g_run_rc==0?"就绪":"运行失败")); while(label[p]&&p<20){pos[p]=label[p];p++;} pos[p++]=' '; pos[p++]='L';pos[p++]='0'+(cr/10)%10;pos[p++]='0'+cr%10;pos[p++]=':';pos[p++]='C';pos[p++]='0'+(cc/10)%10;pos[p++]='0'+cc%10;pos[p]=0; cjk_ui_text(x+18,status_y+3,pos,fg,accent);
        cjk_ui_text_ellipsis(x+w-230,status_y+3,"Ctrl+S 保存  Ctrl+R 运行  Esc 返回",220,fg,accent);
    } else cjk_ui_text_ellipsis(x+18,status_y+3,"Ctrl+N 新建  Enter 打开  Ctrl+R 运行",w-36,fg,accent);
}

int devstudio_on_mouse(int mx,int my,int ldown){
    if(!ldown) return 0;
    if(mx<g_dx||mx>=g_dx+g_dw||my<g_dy||my>=g_dy+g_dh) return 0;
    if(g_mode==M_NEWNAME) return 1;
    if(g_mode==M_LIST){
        if(g_sidebar_w && mx<g_dx+g_sidebar_w){
            int idx=(my-(g_dy+42+52))/36;
            if(idx>=0&&idx<g_nfiles){g_sel=idx;open_file(g_names[idx]);}
            return 1;
        }
        return 1;
    }
    if(g_sidebar_w && mx<g_dx+g_sidebar_w){
        int idx=(my-(g_dy+42+52))/36;
        if(idx>=0&&idx<g_nfiles){g_sel=idx;open_file(g_names[idx]);}
        return 1;
    }
    if(my>=g_code_y&&my<g_code_y+g_code_h){
        int gutter=52,text_x=g_code_x+gutter+16;
        if(mx>=text_x){
            int row=g_top+(my-g_code_y)/g_code_row_h; if(row<0)row=0; int rc=row_count(); if(row>=rc)row=rc-1;
            int col=(mx-text_x)/g_code_char_w; if(col<0)col=0; int rs=row_start(row),re=row_end(row); if(col>re-rs)col=re-rs; g_pos=rs+col; scroll_to_cursor(); return 1;
        }
    }
    return 1;
}
