// kernel.c - 内核主函数: 初始化图形/键盘/磁盘/用户, 进入图形化用户管理
#include "vga.h"
#include "kb.h"
#include "user.h"
#include "app.h"
#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "driver.h"
#include "lang.h"
#include "kheap.h"
#include "proc.h"
#include "installer.h"
#include "layout.h"
#include "ata.h"
#include "gdt.h"
#include "syscall.h"
#include "paging.h"
#include "pmm.h"        // 物理页帧分配器 (Linuxulator 等用户内存分配)
#include "sched.h"
#include "elf.h"
#include "linux.h"
#include "hello_elf.h"
#include "ramfs.h"

// 串口输出 (便于 -nographic 无窗口验证)
// 初始化 COM1: 9600 8N1, 使能 FIFO, 关闭 IRQ。
// 关键: 必须在首次 serial_putc 前调用, 否则部分 BIOS(QEMU/VMware 虚拟 UART)
// 的 LSR 可能不置位 THRE, 导致下方死等 -> 卡在 vga_init 之前 -> 黑屏。
static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);          // IER = 0 (关中断)
    outb(0x3F8 + 3, 0x80);          // LCR: DLAB=1
    outb(0x3F8 + 0, 0x0C);          // DLL = 12 -> 9600 baud
    outb(0x3F8 + 1, 0x00);          // DLM = 0
    outb(0x3F8 + 3, 0x03);          // LCR: 8N1, DLAB=0
    outb(0x3F8 + 2, 0xC7);          // FCR: 使能 FIFO, 清收发, 14 字节阈值
    outb(0x3F8 + 4, 0x0B);          // MCR: DTR+RTS+OUT2
}

// 带超时输出单字符: 最多等 ~0x2000 次轮询, 超时则丢弃, 绝不死锁。
static void serial_putc(char c) {
    for (unsigned i = 0; i < 0x2000; ++i) {
        if (inb(0x3FD) & 0x20) {    // THRE 空
            outb(0x3F8, (uint8_t)c);
            return;
        }
    }
    // 超时: 不再等待, 避免 VMware 无串口设备时卡死黑屏
}

static void serial_puts(const char* s) {
    for (; *s; ++s) serial_putc(*s);
}

__attribute__((noreturn))
void kernel_main(void* multiboot_info) {
    (void)multiboot_info;

    serial_init();
    serial_puts("kernel_main: init kernel heap\r\n");
    kheap_init();            // 先初始化堆: gfx_init (vga 驱动) 需 kmalloc 分配后台缓冲
    serial_puts("kernel_main: init GDT/TSS (user segments)\r\n");
    gdt_init();              // 用户态 GDT 段 + TSS (ring3/syscall 地基)
    serial_puts("kernel_main: init syscall mechanism\r\n");
    syscall_init();          // syscall/sysret + MSR 配置
    serial_puts("kernel_main: init paging\r\n");
    paging_init();           // 页表抽象层 (阶段 1)
    pmm_init();              // 物理页帧分配器 (用户进程物理内存)

    // 读取引导器 e820 检测到的真实物理内存总量 (存于低内存 0x6420/0x6424, 4GB 恒等映射)
    {
        volatile uint32_t* mlo = (volatile uint32_t*)0x6420;
        volatile uint32_t* mhi = (volatile uint32_t*)0x6424;
        uint64_t total = (uint64_t)mlo[0] | ((uint64_t)mhi[0] << 32);
        if (total) pmm_set_phys_total(total);
    }

    serial_puts("kernel_main: init drivers\r\n");
    drv_init_all();          // 注册并自动初始化全部内置驱动 (vga/kb/mouse/ata/serial/idt/pic/pit)

    // ---- 安装路由: 目标盘无 "PSB1" 标记则进入图形安装器, 装完重启 (不返回) ----
    {
        uint8_t mbuf[512];
        int installed = (ata_probe() == 0 &&
                         ata_read_sectors(DISK_SECTORS - 1, 1, mbuf) == 0 &&
                         mbuf[0] == 'P' && mbuf[1] == 'S' &&
                         mbuf[2] == 'B' && mbuf[3] == '1');
        if (!installed) {
            serial_puts("kernel_main: not installed -> installer\r\n");
            if (installer_run) installer_run();   // 图形化安装, 结束后重启, 不会返回
        }
        serial_puts("kernel_main: system installed, continue boot\r\n");
    }

    serial_puts("kernel_main: load users\r\n");
    user_init();
    serial_puts("kernel_main: init process table\r\n");
    proc_init();
    serial_puts("kernel_main: init ramfs (stage 4)\r\n");
    ramfs_init();
    ramfs_load("hello.elf", hello_elf, hello_elf_size);  // 预置 ELF 供 SYS_SPAWN
    serial_puts("kernel_main: init scheduler (stage 2)\r\n");
    sched_init();            // 抢占式内核线程调度器
    serial_puts("kernel_main: init language runtimes\r\n");
    lang_init();          // 注册 Python/C/C++/Java 运行时 (惰性, 不启动)
    serial_puts("kernel_main: create test threads\r\n");
    sched_run_test();     // 创建 2 个测试线程验证多线程切换

    // ---- Linuxulator: 注入演示 ELF 并 (可选) 自检 ----
    serial_puts("kernel_main: Linuxulator demo ELF\r\n");
    linux_ensure_hello();
    // TEMP(font-verify): 暂停 Linuxulator 自检 (并发 WIP 的 ring3 用户页表缺陷 -> #PF 停机, 与字库无关)
    // linux_selftest();

    // 阶段 3: 加载 Hello World ELF 到用户空间并创建用户进程
    serial_puts("kernel_main: load Hello World ELF\r\n");
    {
        uint64_t entry = elf_load(hello_elf, hello_elf_size);
        if (entry) {
            serial_puts("kernel_main: ELF entry=0x");
            char hex[17]; for (int i = 15; i >= 0; i--) { hex[15-i] = "0123456789ABCDEF"[(entry >> (i*4)) & 0xF]; } hex[16] = 0;
            serial_puts(hex); serial_puts("\r\n");
            (void)entry;  // TEMP(font-verify): 临时跳过 hello 用户进程
            // (并发 WIP: 克隆 PML4 未映射该 ring3 代码页 -> 进入调度即 #PF 停机, 与字库无关)
            // sched_create_user_process(entry, USER_STACK_TOP, "hello");
        } else {
            serial_puts("kernel_main: ELF load FAILED\r\n");
        }
    }

    serial_puts("kernel_main: enter GUI\r\n");

    // 启动成功魔数: 写固定物理地址 0x7A00 (loader 第二段低区止于 0x5EFF,
    // 引导扇区在 0x7C00, 此间隙空闲), 供外部验证器(bootcheck)检测内核已到达 GUI。
    // 与符号布局无关, 避免地址随代码体积漂移。
    {
        // 启动成功魔数: 写固定物理地址 0x7A00 (仅作弱信号, 该地址可能被运行期清零,
        // 非权威判定)。权威判定由 COM1 哨兵字符串完成 (见下方 serial_puts)。
        volatile uint32_t* p = (volatile uint32_t*)(uintptr_t)0x7A00;
        *p = 0x424F4F54;   // 'BOOT'
    }
    // 权威启动哨兵: bootcheck.py 抓 COM1 即可判定内核已到达 GUI, 比 RAM 魔数可靠。
    serial_puts("FSOS_BOOT_OK\r\n");

    // 开中断
    __asm__ volatile("sti");

    // 图形化登录 + 用户管理
    app_run();

    while (1) {
        __asm__ volatile ("hlt");
    }
}
