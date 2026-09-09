// driver.c - 统一系统驱动管理
// 内置驱动在 drv_init_all() 中注册并自动初始化; 任一驱动失败只标记状态,
// 不中断后续启动, 保证"一个驱动坏了不影响整体"。
#include "driver.h"
#include "vga.h"
#include "kb.h"
#include "mouse.h"
#include "ata.h"
#include "idt.h"     // idt_init / pic_init / pit_init
#include "io.h"

// 本地调试输出: 直接写 COM1 (0x3F8), 等待 THRE。用于在 VMware 下定位崩溃驱动。
static void dbg_serial(const char* s) {
    while (*s) {
        while (!(inb(0x3FD) & 0x20)) { /* 等 THRE */ }
        outb(0x3F8, (uint8_t)*s++);
    }
}

static driver_t g_drv[DRV_MAX];
static int      g_ndrv = 0;

const char* drv_type_str(drv_type_t t) {
    switch (t) {
        case DRV_DISPLAY: return "display";
        case DRV_INPUT:   return "input";
        case DRV_BLOCK:    return "block";
        case DRV_CHAR:     return "char";
        case DRV_BUS:      return "bus";
        case DRV_MISC:     return "misc";
        default:           return "?";
    }
}

const char* drv_status_str(int s) {
    if (s > 0) return "active";
    if (s < 0) return "failed";
    return "unloaded";
}

// ---- 内置驱动初始化包装 (把各模块的 void init 包成 int(*)(void)) ----
static int d_vga(void)    { vga_init(); return 0; }
static int d_kb(void)     { kb_init();  return 0; }
static int d_mouse(void)  { mouse_init(); return 0; }
static int d_ata(void)    { return ata_probe(); }   // 探测到设备即激活
static int d_serial(void) { return 0; }             // 已在 kernel_main 最早初始化
static int d_idt(void)    { idt_init(); return 0; }
static int d_pic(void)    { pic_init();  return 0; }
static int d_pit(void)    { pit_init(1000); return 0; }

static int drv_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void add_builtin(const char* name, drv_type_t type,
                        int (*init)(void), const char* desc) {
    if (g_ndrv >= DRV_MAX) return;
    driver_t* d = &g_drv[g_ndrv++];
    d->name = name;
    d->type = type;
    d->init = init;
    d->desc = desc;
    d->status = 0;
    d->builtin = 1;
}

void drv_init_all(void) {
    g_ndrv = 0;
    // 注意: CPU 异常/中断控制器 (idt/pic/pit) 必须在所有会注册 IRQ 或可能触发
    // 异常的硬件驱动 (vga/kb/mouse/ata/serial) 之前初始化。否则这些驱动里一旦
    // 访问尚未初始化的 PIC 端口或发生异常, 因无 IDT 处理而 triple fault -> 重启循环。
    add_builtin("idt",    DRV_BUS,     d_idt,    "IDT / exception handlers");
    add_builtin("pic",    DRV_BUS,     d_pic,    "8259A PIC");
    add_builtin("pit",    DRV_MISC,    d_pit,    "PIT timer (1000 Hz)");
    add_builtin("vga",    DRV_DISPLAY, d_vga,    "VGA text/mode13h display");
    add_builtin("kb",     DRV_INPUT,   d_kb,     "PS/2 keyboard (scancode set 1)");
    add_builtin("mouse",  DRV_INPUT,   d_mouse,  "PS/2 mouse");
    add_builtin("ata",    DRV_BLOCK,   d_ata,    "ATA PIO LBA28 block device");
    add_builtin("serial", DRV_CHAR,    d_serial, "COM1 serial console (debug)");

    // 自动初始化全部内置驱动 (带串口标记, 用于定位 VMware 下崩溃的驱动)
    for (int i = 0; i < g_ndrv; i++) {
        dbg_serial("[drv] init: ");
        dbg_serial(g_drv[i].name);
        dbg_serial(" ...\r\n");
        if (g_drv[i].init) {
            int r = g_drv[i].init();
            g_drv[i].status = (r == 0) ? 1 : -1;
            dbg_serial("[drv] done: ");
            dbg_serial(g_drv[i].name);
            dbg_serial(" (r=");
            // 简单整数转字符串
            char buf[12]; int n = 0; int v = r;
            if (v == 0) buf[n++] = '0';
            while (v > 0) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
            buf[n] = 0;
            dbg_serial(buf);
            dbg_serial(")\r\n");
        }
    }
}

int drv_register(const char* name, drv_type_t type,
                 int (*init)(void), const char* desc) {
    if (drv_find(name)) return -1;        // 同名拒绝
    if (g_ndrv >= DRV_MAX) return -1;
    driver_t* d = &g_drv[g_ndrv++];
    d->name = name;
    d->type = type;
    d->init = init;
    d->desc = desc;
    d->status = 0;
    d->builtin = 0;
    return 0;
}

int drv_load(const char* name) {
    driver_t* d = drv_find(name);
    if (!d) return 0;
    if (d->init) {
        int r = d->init();
        d->status = (r == 0) ? 1 : -1;
    }
    return d->status;
}

int        drv_count(void)      { return g_ndrv; }
driver_t*  drv_get(int i)       { return (i >= 0 && i < g_ndrv) ? &g_drv[i] : 0; }
driver_t*  drv_find(const char* name) {
    for (int i = 0; i < g_ndrv; i++)
        if (g_drv[i].name && name && drv_strcmp(g_drv[i].name, name) == 0)
            return &g_drv[i];
    return 0;
}
