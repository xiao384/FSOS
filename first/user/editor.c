// editor.c - FSOS 原生 VSCode 风格代码编辑器
//
// 设计: 暗色主题 (类 VSCode Dark+), 多标签页, 左侧资源管理器侧边栏, 行号,
// 按语言(C/C++/Python/Java)语法高亮, 命令面板(Ctrl+Shift+P), 快速打开(Ctrl+P),
// 鼠标可点标签/侧边栏/文本区定位光标, 块状光标带闪烁。
//
// 文件落在内核文件区 (filesys.h): 最多 16 个、每个 <= 4KB、纯文本, 重启不丢。
// 所有 GUI 应用编译进内核 (first/user/*.c), 无需独立进程。
#include "editor.h"
#include "filesys.h"
#include "layout.h"
#include "gfx.h"
#include "vga.h"
#include "cjk.h"
#include "lang.h"
#include "kb.h"
#include "idt.h"
#include <stdint.h>

// 前向声明 (函数定义顺序靠后)
static int palette_key(int k);
static int name_key(int k);
static int normal_key(int k);
static void draw_overlay(void);
static void start_name(int action);
static void open_quick(void);

// ---------------- 常量 / 布局 ----------------
#define ED_MAX_TABS  8
#define ED_BUF       4096      // 单标签缓冲 (<= FS_MAX_SIZE=4096)
#define ED_NAME      24
#define ED_ROW_H     16       // 每行像素高 (容纳 16px 汉字)
#define ED_GUTTER    28       // 行号槽宽
#define ED_SB_W      84       // 侧边栏宽
#define ED_TAB_H     18       // 标签栏高 (容纳 16px 汉字)
#define ED_STATUS_H  18       // 状态栏高

typedef enum { ED_NORMAL=0, ED_CMD, ED_QUICK, ED_NAMEMODE } ed_mode_t;

typedef struct {
    char name[ED_NAME];
    char buf[ED_BUF];
    int  len;
    int  pos;
    int  top;
    int  dirty;
    uint32_t dir;   // 所属目录块 LBA (LBA_FS_DIR=根目录)
} ed_tab_t;

static ed_tab_t g_tabs[ED_MAX_TABS];
static int g_ntabs = 0;
static int g_cur = 0;

static int       g_mode = ED_NORMAL;
static int       g_sb = 1;                  // 侧边栏可见
static char      g_fnames[FS_MAX_FILES][FS_NAME_SZ];
static int       g_nf = 0;
static int       g_fsel = 0;                // 侧边栏选中项

static char      g_filter[40];
static int       g_flen = 0;
static int       g_psel = 0;
static int       g_match[32];
static int       g_nmatch = 0;

static char      g_prompt[24];
static char      g_newname[ED_NAME];
static int       g_newlen = 0;
static int       g_name_action = 0;        // 0=新建 1=另存为

static char      g_msg[40];
static int       g_skip = 0;

// 绘制时记录的几何 (供鼠标命中)
static int g_cx, g_cy, g_cw, g_ch;
static int g_rows = 10;
static int g_tabx[ED_MAX_TABS], g_tabw[ED_MAX_TABS];

// ---------------- 小工具 ----------------
static int estlen(const char* s){ int n=0; while(s&&s[n])n++; return n; }
static void encpy(char* d,const char* s,int n){ int i=0; for(;i<n-1&&s[i];i++)d[i]=s[i]; for(;i<n;i++)d[i]=0; }
static int elower(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int estrncmp(const char*a,const char*b,int n){ for(int i=0;i<n;i++){ if(a[i]!=b[i])return 0; if(!a[i])return 1; } return 1; }
static int estreq(const char*a,const char*b){ int n=estlen(a); return n==estlen(b)&&estrncmp(a,b,n); }

static const char* ext_of(const char* name){ const char* dot=0; for(const char* p=name;*p;p++) if(*p=='.') dot=p; return dot?dot:""; }
static const char* lang_of(const char* name){
    const char* e=ext_of(name);
    if(e[0]==0) return 0;
    if(elower(e[1])=='p'&&elower(e[2])=='y'&&e[3]==0) return "Python";
    if(elower(e[1])=='c'&&e[2]==0) return "C/C++";
    if(elower(e[1])=='c'&&elower(e[2])=='c'&&e[3]==0) return "C/C++";
    if(elower(e[1])=='c'&&elower(e[2])=='p'&&elower(e[3])=='p'&&e[4]==0) return "C/C++";
    if(elower(e[1])=='j'&&elower(e[2])=='a'&&elower(e[3])=='v'&&elower(e[4])=='a'&&e[5]==0) return "Java";
    return 0;
}
static const char* templ_for(const char* name){
    const char* l = lang_of(name);
    if(!l) return "\n";
    switch(l[0]){
        case 'P': return "print('hello from FSOS')\nfor i in range(3):\n    print(i)\n";
        case 'C': return "// FSOS cint: 整数语义\nint n = 10;\nint s = 0;\nwhile (n > 0) { s = s + n; n = n - 1; }\nprint(s);\n";
        case 'J': return "// FSOS 最小 JVM 演示\nclass Main {\n  main() {\n    print(1);\n  }\n}\n";
        default:  return "\n";
    }
}
static int sub_i(const char* a,const char* sub){
    if(!sub[0]) return 1;
    int al=estlen(a), sl=estlen(sub);
    for(int i=0;i+sl<=al;i++){ int ok=1; for(int j=0;j<sl;j++) if(elower((unsigned char)a[i+j])!=elower((unsigned char)sub[j])){ok=0;break;} if(ok)return 1; }
    return 0;
}
static const char* COMMON_KW =
    "if else for while return break continue switch case default do int char void struct const "
    "static sizeof typedef unsigned long short float double bool true false null None True False "
    "and or not in is print def class import from as pass lambda with try except finally raise "
    "yield global assert del async await new delete this public private protected extends implements "
    "interface package namespace using enum union extern register volatile inline auto ifdef ifndef "
    "endif include define class";
static int kw_match(const char* list,const char* w){
    int n=estlen(w);
    while(*list){
        if(*list==' '){list++;continue;}
        int k=0; while(list[k]&&list[k]!=' ')k++;
        if(k==n && estrncmp(list,w,n)) return 1;
        list+=k;
    }
    return 0;
}

// ---------------- 行/列换算 (当前激活标签) ----------------
static ed_tab_t* A(void){ return &g_tabs[g_cur]; }
static int cur_row(void){ ed_tab_t* t=A(); int r=0; for(int i=0;i<t->pos&&i<t->len;i++) if(t->buf[i]=='\n')r++; return r; }
static int cur_col(void){ ed_tab_t* t=A(); int c=0; for(int i=0;i<t->pos&&i<t->len;i++){ if(t->buf[i]=='\n')c=0; else c++; } return c; }
static int row_start(int row){ ed_tab_t* t=A(); int r=0,i=0; if(row<=0)return 0; for(;i<t->len;i++) if(t->buf[i]=='\n'){ r++; if(r==row)return i+1; } return t->len; }
static int row_end(int row){ ed_tab_t* t=A(); int i=row_start(row); while(i<t->len&&t->buf[i]!='\n')i++; return i; }
static int row_count(void){ ed_tab_t* t=A(); int n=1; for(int i=0;i<t->len;i++) if(t->buf[i]=='\n')n++; return n; }
static void scroll(void){
    ed_tab_t* t=A(); int r=cur_row();
    if(r<t->top) t->top=r;
    else if(r>t->top+g_rows-1) t->top=r-g_rows+1;
}

// ---------------- 编辑操作 ----------------
static void ins_char(char c){
    ed_tab_t* t=A(); if(t->len>=ED_BUF-1)return;
    for(int i=t->len;i>t->pos;i--) t->buf[i]=t->buf[i-1];
    t->buf[t->pos]=c; t->len++; t->pos++; t->buf[t->len]=0; t->dirty=1;
}
static void del_back(void){ ed_tab_t* t=A(); if(t->pos<=0)return; for(int i=t->pos-1;i<t->len-1;i++)t->buf[i]=t->buf[i+1]; t->len--;t->pos--;t->buf[t->len]=0;t->dirty=1; }
static void del_fwd(void){ ed_tab_t* t=A(); if(t->pos>=t->len)return; for(int i=t->pos;i<t->len-1;i++)t->buf[i]=t->buf[i+1]; t->len--;t->buf[t->len]=0;t->dirty=1; }
static void move_up(void){ ed_tab_t* t=A(); int r=cur_row(),c=cur_col(); if(r<=0){t->pos=0;return;} int rs=row_start(r-1),re=row_end(r-1); t->pos=rs+((c<re-rs)?c:(re-rs)); }
static void move_down(void){ ed_tab_t* t=A(); int r=cur_row(),c=cur_col(); int rc=row_count(); if(r>=rc-1){t->pos=t->len;return;} int rs=row_start(r+1),re=row_end(r+1); t->pos=rs+((c<re-rs)?c:(re-rs)); }

// ---------------- 文件操作 ----------------
static void refresh_files(void){ fs_init(); g_nf=fs_list(g_fnames,FS_MAX_FILES); if(g_fsel>=g_nf)g_fsel=g_nf-1; if(g_fsel<0)g_fsel=0; }
static void write_index(void){
    char idx[512]; int p=0;
    int buf_cap = SCREEN_H * 480 / 200;  // hires: 按屏幕高度缩放缓冲区上限
    for(int i=0;i<g_nf&&p<buf_cap;i++){ for(int j=0;g_fnames[i][j]&&p<buf_cap;j++) idx[p++]=g_fnames[i][j]; idx[p++]='\n'; }
    idx[p]=0; fs_write("INDEX.TXT",idx);
}
static void open_tab(const char* name, uint32_t dir){
    for(int i=0;i<g_ntabs;i++) if(estreq(g_tabs[i].name,name) && g_tabs[i].dir==dir){ g_cur=i; return; }
    if(g_ntabs>=ED_MAX_TABS){ encpy(g_msg,"标签已满(最多8)",sizeof(g_msg)); return; }
    ed_tab_t* t=&g_tabs[g_ntabs];
    encpy(t->name,name,ED_NAME);
    t->dir=dir;
    int n=fs_read_in(dir,name,t->buf,ED_BUF);
    if(n<0){ t->len=0; t->buf[0]=0; encpy(g_msg,"读取失败",sizeof(g_msg)); }
    else { t->len=n; encpy(g_msg,"已打开",sizeof(g_msg)); }
    t->pos=0; t->top=0; t->dirty=0;
    g_cur=g_ntabs++;
}
static void save_active(void){
    ed_tab_t* t=A();
    if(!t->name[0]){ encpy(g_msg,"无文件名",sizeof(g_msg)); return; }
    t->buf[t->len]=0;
    int rc=fs_write_in(t->dir,t->name,t->buf);
    if(rc==0){ t->dirty=0; refresh_files(); write_index(); encpy(g_msg,"已保存",sizeof(g_msg)); }
    else if(rc==-3) encpy(g_msg,"目录已满(最多16)",sizeof(g_msg));
    else if(rc==-4) encpy(g_msg,"数据区已满(4KB)",sizeof(g_msg));
    else encpy(g_msg,"保存失败",sizeof(g_msg));
}
static void save_as(const char* name){ ed_tab_t* t=A(); encpy(t->name,name,ED_NAME); save_active(); }
static void run_active(void){
    ed_tab_t* t=A(); const char* lang=lang_of(t->name);
    if(!lang){ encpy(g_msg,"未知类型: 需 .py/.c/.java",sizeof(g_msg)); return; }
    if(t->dirty) save_active();
    t->buf[t->len]=0;
    lang_launch(lang,t->buf,t->name);
    g_skip=1;
    encpy(g_msg,"运行结束",sizeof(g_msg));
}
static void close_tab(void){
    if(g_ntabs<=0)return;
    for(int i=g_cur;i<g_ntabs-1;i++) g_tabs[i]=g_tabs[i+1];
    g_ntabs--;
    if(g_cur>=g_ntabs) g_cur=g_ntabs-1;
    if(g_cur<0) g_cur=0;
}

// ---------------- 命令面板 / 快速打开 ----------------
static const char* CMD[] = {
    "保存 (Ctrl+S)","另存为","新建文件 (Ctrl+N)","打开文件 (Ctrl+P)",
    "运行 (Ctrl+R)","切换侧栏 (Ctrl+B)","关闭标签 (Ctrl+W)","全选"
};
#define NCMD (int)(sizeof(CMD)/sizeof(CMD[0]))
static void recompute(void){
    g_nmatch=0;
    if(g_mode==ED_QUICK){
        for(int i=0;i<g_nf&&g_nmatch<32;i++) if(g_flen==0||sub_i(g_fnames[i],g_filter)) g_match[g_nmatch++]=i;
    } else if(g_mode==ED_CMD){
        for(int i=0;i<NCMD&&g_nmatch<32;i++) if(g_flen==0||sub_i(CMD[i],g_filter)) g_match[g_nmatch++]=i;
    }
}
static void do_cmd(int idx){
    switch(idx){
        case 0: save_active(); break;
        case 1: start_name(1); break;
        case 2: start_name(0); break;
        case 3: open_quick(); break;
        case 4: run_active(); break;
        case 5: g_sb=!g_sb; encpy(g_msg,"侧栏已切换",sizeof(g_msg)); break;
        case 6: close_tab(); break;
        case 7: if(g_ntabs>0) A()->pos=A()->len; break;
    }
}
static void open_quick(void){ g_mode=ED_QUICK; g_flen=0; g_filter[0]=0; g_psel=0; refresh_files(); }
static void open_cmd(void){ g_mode=ED_CMD; g_flen=0; g_filter[0]=0; g_psel=0; }
static void start_name(int action){ g_mode=ED_NAMEMODE; g_name_action=action; g_newlen=0; g_newname[0]=0; encpy(g_prompt,action?"另存为:":"新建文件名:",sizeof(g_prompt)); }
static void finalize_name(void){
    if(g_newlen<=0){ g_mode=ED_NORMAL; return; }
    g_newname[g_newlen]=0;
    if(g_name_action==0){
        if(g_ntabs>=ED_MAX_TABS){ encpy(g_msg,"标签已满(最多8)",sizeof(g_msg)); g_mode=ED_NORMAL; return; }
        ed_tab_t* t=&g_tabs[g_ntabs];
        encpy(t->name,g_newname,ED_NAME);
        const char* tpl=templ_for(t->name);
        int i=0; for(;tpl[i]&&i<ED_BUF-1;i++) t->buf[i]=tpl[i];
        t->buf[i]=0; t->len=i; t->pos=0; t->top=0; t->dirty=1; t->dir=LBA_FS_DIR;
        g_cur=g_ntabs++;
    } else {
        save_as(g_newname);
    }
    g_mode=ED_NORMAL;
}

// ---------------- 键盘 ----------------
int editor_key(int k){
    if(g_mode==ED_CMD||g_mode==ED_QUICK) return palette_key(k);
    if(g_mode==ED_NAMEMODE) return name_key(k);
    return normal_key(k);
}
static int palette_key(int k){
    recompute();
    if(k==27){ g_mode=ED_NORMAL; return 1; }              // ESC 取消
    if(k==8){ if(g_flen>0)g_flen--; g_psel=0; return 1; }  // BS
    if(k==KEY_UP){ if(g_psel>0)g_psel--; return 1; }
    if(k==KEY_DOWN){ if(g_psel<g_nmatch-1)g_psel++; return 1; }
    if(k==13){                                              // Enter 执行
        if(g_nmatch>0){
            if(g_mode==ED_QUICK) open_tab(g_fnames[g_match[g_psel]], LBA_FS_DIR);
            else do_cmd(g_match[g_psel]);
        }
        g_mode=ED_NORMAL; return 1;
    }
    if(k>=32&&k<=126&&g_flen<39){ g_filter[g_flen++]=(char)k; g_psel=0; return 1; }
    return 1;
}
static int name_key(int k){
    if(k==27){ g_mode=ED_NORMAL; return 1; }
    if(k==13){ finalize_name(); return 1; }
    if(k==8){ if(g_newlen>0)g_newlen--; return 1; }
    if(k>=32&&k<=126&&g_newlen<ED_NAME-1){ g_newname[g_newlen++]=(char)k; return 1; }
    return 1;
}
static int normal_key(int k){
    int mods = kb_mods();
    if(k==16){ if(mods&KB_MOD_SHIFT) open_cmd(); else open_quick(); return 1; } // Ctrl+P / Ctrl+Shift+P
    if(k==2){ g_sb=!g_sb; return 1; }        // Ctrl+B 侧栏
    if(k==19){ save_active(); return 1; }    // Ctrl+S
    if(k==14){ start_name(0); return 1; }    // Ctrl+N 新建
    if(k==18){ run_active(); return 1; }     // Ctrl+R 运行
    if(k==23){ close_tab(); return 1; }      // Ctrl+W 关闭标签
    if(g_ntabs==0){ if(k==27) return 0; return 1; } // 无标签时吞掉除 ESC 外按键

    ed_tab_t* t=A();
    if(k==27){ g_msg[0]=0; return 1; }       // ESC 不清空编辑, 仅消提示
    if(k==13){ ins_char('\n'); scroll(); return 1; }
    if(k==8){ del_back(); scroll(); return 1; }
    if(k==KEY_DEL){ del_fwd(); scroll(); return 1; }
    if(k==KEY_LEFT){ if(t->pos>0)t->pos--; scroll(); return 1; }
    if(k==KEY_RIGHT){ if(t->pos<t->len)t->pos++; scroll(); return 1; }
    if(k==KEY_UP){ move_up(); scroll(); return 1; }
    if(k==KEY_DOWN){ move_down(); scroll(); return 1; }
    if(k==KEY_HOME){ t->pos=row_start(cur_row()); scroll(); return 1; }
    if(k==KEY_END){ t->pos=row_end(cur_row()); scroll(); return 1; }
    if(k==KEY_PGUP){ t->top-=(g_rows-1); if(t->top<0)t->top=0; return 1; }
    if(k==KEY_PGDN){ t->top+=(g_rows-1); scroll(); return 1; }
    if(k==9){ ins_char(' '); ins_char(' '); scroll(); return 1; } // Tab -> 两空格
    if(k>=32&&k<=126){ ins_char((char)k); scroll(); return 1; }
    return 1;
}

// ---------------- 绘制: 语法高亮单行 ----------------
static void draw_hl(int x,int y,const char* s,int slen,const char* lang,uint8_t bg){
    int i=0; int is_py = lang && lang[0]=='P';
    (void)is_py;
    while(i<slen){
        char c=s[i];
        if(c==' '||c=='\t'){ i++; continue; }
        if(c=='/'&&i+1<slen&&s[i+1]=='/'){ cjk_text(x,y,s+i,COL_DGRAY,bg); return; }   // 行注释
        if(c=='#'){ cjk_text(x,y,s+i,COL_LCYAN,bg); return; }                          // # 注释/预处理
        if(c=='"'||c=='\''){
            int j=i+1; while(j<slen && s[j]!=c){ if(s[j]=='\\'&&j+1<slen)j++; j++; }
            if(j<slen)j++;
            char tmp[64]; int q=0; for(int m=i;m<j&&q<63;m++)tmp[q++]=s[m]; tmp[q]=0;
            cjk_text(x,y,tmp,COL_LGREEN,bg); x+=cjk_text_w(tmp); i=j; continue;
        }
        if((c>='0'&&c<='9')||(c=='.'&&i+1<slen&&s[i+1]>='0'&&s[i+1]<='9')){
            int j=i; while(j<slen&&((s[j]>='0'&&s[j]<='9')||s[j]=='.'||s[j]=='x'||(s[j]>='a'&&s[j]<='f')||(s[j]>='A'&&s[j]<='F')))j++;
            char tmp[32]; int q=0; for(int m=i;m<j&&q<31;m++)tmp[q++]=s[m]; tmp[q]=0;
            cjk_text(x,y,tmp,COL_YELLOW,bg); x+=cjk_text_w(tmp); i=j; continue;
        }
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'){
            int j=i; while(j<slen&&((s[j]>='A'&&s[j]<='Z')||(s[j]>='a'&&s[j]<='z')||(s[j]>='0'&&s[j]<='9')||s[j]=='_'))j++;
            char w[32]; int q=0; for(int m=i;m<j&&q<31;m++)w[q++]=s[m]; w[q]=0;
            uint8_t col = kw_match(COMMON_KW,w)?COL_LBLUE:COL_LGRAY;
            cjk_text(x,y,w,col,bg); x+=cjk_text_w(w); i=j; continue;
        }
        char tmp[2]={c,0}; cjk_text(x,y,tmp,COL_LGRAY,bg); x+=cjk_text_w(tmp); i++;
    }
}

// ---------------- 绘制 ----------------
void editor_draw(int x,int y,int w,int h){
    g_cx=x; g_cy=y; g_cw=w; g_ch=h;
    // 暗色背景
    gfx_fill_idx(x,y,x+w-1,y+h-1,COL_BLACK);

    int ty0 = y + ED_TAB_H;
    int sb_w = g_sb ? ED_SB_W : 0;
    int ex = x + sb_w;
    int gutter_x = ex;
    int text_x = ex + ED_GUTTER;
    int status_y = y + h - ED_STATUS_H;
    g_rows = (status_y - ty0) / ED_ROW_H;
    if(g_rows<1) g_rows=1;

    // ---- 标签栏 ----
    gfx_fill_idx(x, y, x+w-1, ty0-1, COL_DGRAY);
    int tx = x+2;
    for(int i=0;i<g_ntabs;i++){
        ed_tab_t* t=&g_tabs[i];
        char lab[ED_NAME+1]; int p=0;
        for(int j=0;t->name[j]&&p<ED_NAME-1;j++) lab[p++]=t->name[j];
        if(t->dirty) lab[p++]='*';
        lab[p]=0;
        int tw = cjk_text_w(lab)+14;
        g_tabx[i]=tx; g_tabw[i]=tw;
        uint8_t bg = (i==g_cur)?COL_ACCENT:COL_DGRAY;
        uint8_t fg = (i==g_cur)?COL_WHITE:COL_LGRAY;
        gfx_fill_idx(tx, y+1, tx+tw-1, ty0-2, bg);
        cjk_text(tx+3, y+1, lab, fg, bg);
        if(i==g_cur) cjk_text(tx+tw-9, y+1, "x", COL_WHITE, bg); // 关闭标记
        tx += tw + 3;
    }

    // ---- 侧边栏 ----
    if(g_sb){
        gfx_fill_idx(x, ty0, x+sb_w-1, status_y-1, COL_DGRAY);
        cjk_text(x+4, ty0+2, "资源管理器", COL_WHITE, COL_DGRAY);
        gfx_rect_idx(x+2, ty0+19, x+sb_w-3, ty0+20, COL_ACCENT);
        int ly = ty0+22;
        for(int i=0;i<g_nf;i++){
            if(ly+16 > status_y-2) break;
            if(i==g_fsel) gfx_fill_idx(x+2, ly, x+sb_w-3, ly+15, COL_ACCENT_SOFT);
            uint8_t fg=(i==g_fsel)?COL_ACCENT:COL_LGRAY;
            cjk_text_ellipsis(x+6, ly, g_fnames[i], sb_w-12, fg, (i==g_fsel)?COL_ACCENT_SOFT:COL_DGRAY);
            ly += 16;
        }
    }

    // ---- 文本区 ----
    if(g_ntabs>0){
        ed_tab_t* t=A();
        const char* lang = lang_of(t->name);
        int rc = row_count();
        if(t->top>rc-1) t->top=rc-1;
        if(t->top<0) t->top=0;
        int crow = cur_row();
        for(int r=0;r<g_rows;r++){
            int rr = t->top + r;
            int ry = ty0 + r*ED_ROW_H;
            int is_cur = (rr==crow);
            uint8_t lbg = is_cur ? COL_DGRAY : COL_BLACK;
            // 整行背景
            gfx_fill_idx(gutter_x, ry, x+w-1, ry+ED_ROW_H-1, lbg);
            // 行号
            if(rr<rc){
                char num[8]; int q=0; int v=rr+1;
                if(v==0) v=1;
                char tmp[8]; int n=0; int vv=v;
                if(vv==0){ tmp[n++]='0'; }
                while(vv>0){ tmp[n++]='0'+vv%10; vv/=10; }
                for(int m=n-1;m>=0;m--){ num[q++]=tmp[m]; }
                num[q]=0;
                int nw=cjk_text_w(num);
                cjk_text(gutter_x+ED_GUTTER-nw-2, ry, num, COL_DGRAY, lbg);
            }
            // 内容
            if(rr<rc){
                int rs=row_start(rr), re=row_end(rr);
                int n=0; char line[256]; for(int i=rs;i<re&&n<255;i++) line[n++]=t->buf[i]; line[n]=0;
                draw_hl(text_x, ry, line, n, lang, lbg);
                // 光标 (块状, 闪烁)
                if(is_cur){
                    uint8_t phase = (uint8_t)((get_ticks()/450)&1);
                    if(phase){
                        int cx = text_x + cur_col()*8;
                        gfx_fill_idx(cx, ry+4, cx+5, ry+ED_ROW_H-4, COL_LGRAY);
                    }
                }
            }
        }
    } else {
        cjk_text(text_x, ty0+4, "无打开的文件", COL_LGRAY, COL_BLACK);
        cjk_text(text_x, ty0+22, "Ctrl+P 快速打开  Ctrl+N 新建", COL_DGRAY, COL_BLACK);
        cjk_text(text_x, ty0+40, "Ctrl+Shift+P 命令面板  Ctrl+B 侧栏", COL_DGRAY, COL_BLACK);
    }

    // ---- 状态栏 ----
    gfx_fill_idx(x, status_y, x+w-1, y+h-1, COL_ACCENT);
    ed_tab_t* t = (g_ntabs>0)?A():0;
    char s[64]; int p=0;
    if(t){
        for(int i=0;t->name[i]&&p<20;i++) s[p++]=t->name[i];
        if(t->dirty) s[p++]='*';
    } else {
        for(int i=0;g_msg[i]&&p<22;i++) s[p++]=g_msg[i];
    }
    s[p]=0;
    cjk_text(x+4, status_y+1, s, COL_WHITE, COL_ACCENT);
    if(t){
        char pos[16]; int q=0; int cr=cur_row()+1, cc=cur_col()+1;
        pos[q++]='L'; pos[q++]='0'+cr/10; pos[q++]='0'+cr%10;
        pos[q++]=':';
        pos[q++]='C'; pos[q++]='0'+cc/10; pos[q++]='0'+cc%10;
        pos[q]=0;
        cjk_text(x+w-70, status_y+1, pos, COL_WHITE, COL_ACCENT);
    }

    // ---- 悬浮提示 ----
    if(g_msg[0] && g_ntabs>0){
        int mw=cjk_text_w(g_msg);
        cjk_text(x+w-mw-4, ty0+2, g_msg, COL_YELLOW, COL_BLACK);
    }

    // ---- 命令面板 / 快速打开 / 文件名输入 覆盖层 ----
    if(g_mode==ED_CMD||g_mode==ED_QUICK||g_mode==ED_NAMEMODE) draw_overlay();
}

static void draw_overlay(void){
    int bw=224, bh=140;
    int bx=g_cx+(g_cw-bw)/2, by=g_cy+(g_ch-bh)/2;
    gfx_fill_idx(bx,by,bx+bw-1,by+bh-1,COL_DGRAY);
    gfx_rect_idx(bx,by,bx+bw-1,by+bh-1,COL_ACCENT);
    const char* title = (g_mode==ED_QUICK)?"快速打开文件":(g_mode==ED_NAMEMODE?g_prompt:"命令面板 (输入过滤)");
    cjk_text(bx+4,by+3,title,COL_WHITE,COL_DGRAY);
    // 输入行
    gfx_fill_idx(bx+4,by+22,bx+bw-5,by+40,COL_BLACK);
    gfx_rect_idx(bx+4,by+22,bx+bw-5,by+40,COL_ACCENT);
    const char* inp = (g_mode==ED_NAMEMODE)?g_newname:g_filter;
    cjk_text(bx+7,by+26,inp,COL_WHITE,COL_BLACK);
    int il=estlen(inp);
    gfx_fill_idx(bx+6+8*il,by+26,bx+7+8*il,by+38,COL_LGRAY);
    // 列表
    recompute();
    int ly=by+46;
    for(int i=0;i<g_nmatch && (ly+16)<(by+bh-4);i++){
        int idx=g_match[i];
        const char* label = (g_mode==ED_QUICK)?g_fnames[idx]:CMD[idx];
        int sel=(i==g_psel);
        if(sel) gfx_fill_idx(bx+4,ly,bx+bw-5,ly+15,COL_ACCENT_SOFT);
        cjk_text(bx+8,ly+1,label, sel?COL_ACCENT:COL_LGRAY, sel?COL_ACCENT_SOFT:COL_DGRAY);
        ly+=18;
    }
    if(g_nmatch==0) cjk_text(bx+8,by+46,"(无匹配)",COL_DGRAY,COL_DGRAY);
}

// ---------------- 鼠标 ----------------
int editor_on_mouse(int mx,int my,int ldown){
    if(ldown==0) return 0;
    if(g_mode!=ED_NORMAL) return 0; // 覆盖层用键盘操作
    int rx=mx-g_cx, ry=my-g_cy;
    if(rx<0||ry<0||rx>=g_cw||ry>=g_ch) return 0;

    // 标签栏
    if(ry < ED_TAB_H){
        for(int i=0;i<g_ntabs;i++){
            if(rx>=g_tabx[i] && rx<=g_tabx[i]+g_tabw[i]){
                if(i==g_cur && rx>=g_tabx[i]+g_tabw[i]-9){ close_tab(); }  // 点关闭标记
                else { g_cur=i; }
                return 1;
            }
        }
        return 1;
    }

    int ty0 = g_cy + ED_TAB_H;
    int sb_w = g_sb ? ED_SB_W : 0;
    int ex = g_cx + sb_w;
    int text_x = ex + ED_GUTTER;

    // 侧边栏文件列表
    if(g_sb && rx < ED_SB_W){
        int idx = (ry - (ty0+22)) / 16;
        if(idx>=0 && idx<g_nf){ g_fsel=idx; open_tab(g_fnames[idx], LBA_FS_DIR); }
        return 1;
    }

    // 文本区: 定位光标
    if(g_ntabs==0) return 1;
    ed_tab_t* t=A();
    int r = t->top + (ry - ty0)/ED_ROW_H;
    int rc = row_count();
    if(r<0) r=0;
    if(r>=rc) r=rc-1;
    int rs=row_start(r), re=row_end(r);
    int line_len = re-rs;
    int col = (rx - text_x)/8;
    if(col<0) col=0;
    if(col>line_len) col=line_len;
    t->pos = rs + col;
    scroll();
    return 1;
}

// ---------------- 其余回调 ----------------
void editor_open(void){
    refresh_files();
    g_mode=ED_NORMAL;
    if(g_ntabs==0) encpy(g_msg,"Ctrl+P 打开  Ctrl+N 新建  Ctrl+B 侧栏",sizeof(g_msg));
}
void editor_open_file(const char* name){
    if(!name||!name[0]) return;
    open_tab(name, LBA_FS_DIR);
}
// 打开指定目录下的文件到新标签 (供文件管理器进入子目录后调用)
void editor_open_file_in(const char* name, uint32_t dir){
    if(!name||!name[0]) return;
    open_tab(name, dir);
}
int editor_tick(void){
    static uint32_t last=0;
    uint32_t b=get_ticks()/450;
    if(b!=last){ last=b; return 1; }
    return 0;
}
int editor_take_skip(void){ int s=g_skip; g_skip=0; return s; }
