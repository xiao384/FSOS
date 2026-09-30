// taskmgr.c - 任务管理器
//
// 功能:
//   - 展示三类进程: 系统必要 / 系统非必要 / 程序进程
//   - 显示电脑性能: CPU 利用率 (基于空闲 tick 采样), 内存占用 (kheap), 运行时间
//   - 支持结束进程 (Up/Down 选中, K/E 结束), 系统必要进程拒绝结束
//   - 支持恢复已结束的进程 (R)
//
// 说明: FSOS 为单执行流内核, 这里的"进程"是进程注册表模型 (proc.c),
// 程序进程拥有真实内核堆缓冲, 结束时 kfree 释放, 内存占用会真实下降。
#include "taskmgr.h"
#include "proc.h"
#include "vga.h"
#include "theme_api.h"   // gui_framework Phase 10: Theme API 集成点
#include "cjk.h"        // ui_polish: 中文文案绘制
#include "gui.h"
#include "kb.h"
#include "idt.h"
#include "kheap.h"
#include "pmm.h"

// 布局常量
#define COLHDR_Y  72
#define LIST_TOP  82
#define LIST_BOT  186
#define LH        9
#define BAR_X     40
#define BAR_W     110

// 实时性能 (全局, 供绘制使用)
static int  g_cpu_pct = 0;     // 系统 CPU 利用率 0..100
static uint32_t g_mem_used = 0;
static uint32_t g_mem_total = 0;

// 简单无符号整数格式化 (右对齐, 最小宽度 minw)
static void uitoa(uint32_t v, char* buf, int minw) {
    char tmp[12]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n / 2; i++) { char c = tmp[i]; tmp[i] = tmp[n - 1 - i]; tmp[n - 1 - i] = c; }
    int p = 0;
    for (int i = 0; i < minw - n; i++) buf[p++] = ' ';
    for (int i = 0; i < n; i++) buf[p++] = tmp[i];
    buf[p] = '\0';
}

static void draw_bar(int x, int y, int w, int h, int pct, uint8_t fill) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    vga_draw_rect(x, y, x + w - 1, y + h - 1, COL_DGRAY);
    int fw = (w - 2) * pct / 100;
    if (fw > 0) vga_fill_rect(x + 1, y + 1, x + fw, y + h - 2, fill);
}

static const char* type_group_label(proc_type_t t) {
    switch (t) {
        case PROC_SYS_NECESSARY: return "== 系统：必要进程 ==";
        case PROC_SYS_OPTIONAL:  return "== 系统：非必要进程 ==";
        default:                 return "== 程序进程 ==";
    }
}

// 计算 proc i 在扁平行表中的行号 (含分组头行)
static int row_of_proc(int i) {
    int r = 0;
    for (int j = 0; j <= i; j++) {
        if (j == 0 || proc_get(j)->type != proc_get(j - 1)->type) r++;  // 分组头行
        if (j == i) return r;                                           // proc 行
        r++;                                                            // 越过 proc 行
    }
    return r;
}
static int total_rows(void) {
    int n = proc_count();
    return n ? row_of_proc(n - 1) + 1 : 0;
}

static void draw_perf(void) {
    // 内存快照 (真实物理内存: 总量来自 e820, 已用 = 内核镜像 + 堆 + 物理帧)
    g_mem_used  = (uint32_t)phys_mem_used_bytes();
    g_mem_total = (uint32_t)phys_mem_total_bytes();

    vga_fill_rect(4, 16, 316, 68, COL_PANEL);
    vga_draw_rect(4, 16, 316, 68, COL_LBLUE);
    cjk_text(8, 14, "性能", COL_YELLOW, COL_PANEL);

    // CPU
    vga_draw_text(8, 30, "CPU", COL_WHITE, COL_PANEL);
    draw_bar(BAR_X, 30, BAR_W, 9, g_cpu_pct, COL_UI_SOFT_OK);
    char buf[16]; uitoa((uint32_t)g_cpu_pct, buf, 3);
    vga_draw_text(BAR_X + BAR_W + 4, 30, buf, COL_WHITE, COL_PANEL);
    vga_draw_text(BAR_X + BAR_W + 18, 30, "%", COL_WHITE, COL_PANEL);

    // MEM
    cjk_text(8, 40, "内存", COL_WHITE, COL_PANEL);
    int mpct = g_mem_total ? (int)(100ULL * g_mem_used / g_mem_total) : 0;
    draw_bar(BAR_X, 44, BAR_W, 9, mpct, COL_LCYAN);
    char s1[12], s2[12];
    uitoa(g_mem_used / 1024, s1, 6);
    uitoa(g_mem_total / 1024, s2, 6);
    vga_draw_text(BAR_X + BAR_W + 4, 44, s1, COL_WHITE, COL_PANEL);
    vga_draw_text(BAR_X + BAR_W + 32, 44, "/", COL_LGRAY, COL_PANEL);
    vga_draw_text(BAR_X + BAR_W + 40, 44, s2, COL_WHITE, COL_PANEL);
    vga_draw_text(BAR_X + BAR_W + 70, 44, "KB", COL_LGRAY, COL_PANEL);

    // 统计行
    uint32_t up = get_ticks() / 1000;
    int running = 0;
    for (int i = 0; i < proc_count(); i++)
        if (proc_get(i)->state == PROC_RUNNING) running++;
    char line[80]; int p = 0;
    const char* u = "运行 ";
    const char* q;
    while (*u) line[p++] = *u++;
    uitoa(up, buf, 0); q = buf; while (*q) line[p++] = *q++;
    u = "秒 | 进程 "; while (*u) line[p++] = *u++;
    uitoa((uint32_t)proc_count(), buf, 0); q = buf; while (*q) line[p++] = *q++;
    u = " | 运行 "; while (*u) line[p++] = *u++;
    uitoa((uint32_t)running, buf, 0); q = buf; while (*q) line[p++] = *q++;
    u = " | CPU "; while (*u) line[p++] = *u++;
    uitoa((uint32_t)g_cpu_pct, buf, 0); q = buf; while (*q) line[p++] = *q++;
    u = "%"; while (*u) line[p++] = *u++;
    line[p] = '\0';
    cjk_text(8, 54, line, COL_LGRAY, COL_PANEL);
}

static void draw_list(int scrollTop, int vis, int sel) {
    cjk_text(6, COLHDR_Y - 4, "PID  名称            状态    CPU  内存", COL_YELLOW, COL_UI_BG_SOFT);
    vga_draw_rect(4, COLHDR_Y + 8, 316, COLHDR_Y + 8, COL_LBLUE);

    int n = proc_count();
    for (int i = 0; i < n; i++) {
        proc_t* p = proc_get(i);
        int row = row_of_proc(i);

        // 分组头
        if (i == 0 || p->type != proc_get(i - 1)->type) {
            int hr = row - 1;
            if (hr >= scrollTop && hr < scrollTop + vis) {
                int y = LIST_TOP + (hr - scrollTop) * LH;
                cjk_text(6, y - 4, type_group_label(p->type), COL_LMAG, COL_UI_BG_SOFT);
            }
        }
        // 进程行
        if (row < scrollTop || row >= scrollTop + vis) continue;
        int y = LIST_TOP + (row - scrollTop) * LH;

        if (i == sel) vga_fill_rect(4, y - 1, 316, y + 7, COL_PANEL_HI);

        char pid[6]; uitoa((uint32_t)p->pid, pid, 3);
        vga_draw_text(6, y, pid, COL_WHITE, i == sel ? COL_PANEL_HI : COL_UI_BG_SOFT);

        // 名称截断 16
        char nm[17]; int j = 0;
        while (p->name[j] && j < 16) { nm[j] = p->name[j]; j++; }
        nm[j] = '\0';
        uint8_t nmc = (p->state == PROC_TERMINATED) ? COL_DGRAY : COL_WHITE;
        vga_draw_text(30, y, nm, nmc, i == sel ? COL_PANEL_HI : COL_UI_BG_SOFT);

        vga_draw_text(150, y, proc_state_str(p->state),
                      p->state == PROC_TERMINATED ? COL_UI_SOFT_ERR : COL_UI_SOFT_OK,
                      i == sel ? COL_PANEL_HI : COL_UI_BG_SOFT);

        char cp[8]; uitoa((uint32_t)(p->cpu_permille / 10), cp, 3);
        vga_draw_text(232, y, cp, COL_YELLOW, i == sel ? COL_PANEL_HI : COL_UI_BG_SOFT);
        vga_draw_text(248, y, "%", COL_LGRAY, i == sel ? COL_PANEL_HI : COL_UI_BG_SOFT);

        char mk[8]; uitoa(p->mem_kb, mk, 5);
        vga_draw_text(268, y, mk, COL_CYAN, i == sel ? COL_PANEL_HI : COL_UI_BG_SOFT);
        vga_draw_text(300, y, "K", COL_LGRAY, i == sel ? COL_PANEL_HI : COL_UI_BG_SOFT);
    }
}

static void set_status(char* st, const char* msg) {
    int i = 0;
    while (msg[i] && i < 38) { st[i] = msg[i]; i++; }
    st[i] = '\0';
}

static void do_kill(int sel, char* st) {
    proc_t* p = proc_get(sel);
    if (!p) return;
    if (!p->killable) {
        set_status(st, "系统必要进程：不能结束");
        return;
    }
    if (p->state != PROC_RUNNING) {
        set_status(st, "已结束/挂起");
        return;
    }
    int ok = gui_dialog_confirm("结束进程",
                                "结束选中的进程？", "结束", "取消");
    if (!ok) return;
    int r = proc_kill(sel);
    if (r == 0) set_status(st, "已结束（内存已释放）");
}

void taskmgr_run(void) {
    proc_init();
    int sel = 0;
    int scrollTop = 0;
    char status[48]; status[0] = '\0';

    for (;;) {
        uint32_t t0 = get_ticks(), i0 = get_idle_ticks();

        vga_clear(COL_UI_BG_SOFT);
        gui_title_bar("任务管理器", "性能");

        draw_perf();

        int total = total_rows();
        int vis = (LIST_BOT - LIST_TOP) / LH;
        int srow = row_of_proc(sel);
        scrollTop = srow - vis / 2;
        if (scrollTop < 0) scrollTop = 0;
        if (scrollTop > total - vis) scrollTop = total - vis;
        if (scrollTop < 0) scrollTop = 0;

        draw_list(scrollTop, vis, sel);

        const char* hint = status[0] ? status
                                      : "上/下 移动 | K/E 结束 | R 恢复 | ESC 退出";
        uint8_t hc = status[0] ? COL_YELLOW : COL_LGRAY;
        gui_status_bar(hint, hc);
        gfx_flip();              // 刷新画面 (taskmgr 用 kb_poll 非阻塞, 需手动翻页)

        // 空闲到约 250ms (计入空闲统计), 使 CPU 利用率真实
        while (get_ticks() - t0 < 250) cpu_idle_halt();

        uint32_t t1 = get_ticks(), i1 = get_idle_ticks();
        uint32_t dt = t1 - t0, di = (uint32_t)(i1 - i0);
        int cpu = dt ? (int)(100ULL * (dt - di) / dt) : 0;
        if (cpu < 0) cpu = 0;
        if (cpu > 100) cpu = 100;
        g_cpu_pct = cpu;

        proc_tick(250);

        int k = kb_poll();
        if (k) {
            status[0] = '\0';
            if (k == KEY_ESC) return;
            else if (k == KEY_UP)   sel = (sel + proc_count() - 1) % proc_count();
            else if (k == KEY_DOWN) sel = (sel + 1) % proc_count();
            else if (k == 'k' || k == 'e' || k == KEY_DEL) do_kill(sel, status);
            else if (k == 'r' || k == 'R') {
                int r = proc_resume(sel);
                if (r == 0) set_status(status, "进程已恢复");
                else if (r == -1) set_status(status, "系统必要进程：不能恢复");
            }
        }
    }
}
