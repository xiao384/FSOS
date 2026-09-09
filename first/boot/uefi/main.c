// main.c - FSOS UEFI 引导加载器 (BOOTX64.EFI)
//
// 设计 (对标 Windows/Linux 的现代引导, 让 FSOS 成为"装在硬盘上的 OS"而非 Live/PE 镜像):
//   1. 由 EFI 固件把本文件 (EFI/BOOT/BOOTX64.EFI) 从 ESP 文件系统加载到内存;
//   2. 本加载器打开同一 ESP 卷, 按【文件名】读取 KERNEL.BIN (内核文件), 拷入 0x100000;
//      —— 内核是磁盘上的普通文件, 可单独替换/升级, 不再焊死在固定 LBA 上;
//   3. 自建 4 级页表, 2MB 大页一致映射 0~4GB (满足内核/显卡需求);
//   4. 退出 Boot Services (释放 UEFI 页表, 切换自有页表);
//   5. 重装载数据段 (对齐 BIOS loader 的 ds/es/ss=0x10), 关闭中断, 跳入内核入口 0x100000
//      (已是 64 位长模式, 段均为扁平)。
//
// 内核数据区 (layout.h: LBA 3800..4095 等) 保持原样: 内核仍用自身 ATA PIO 驱动按 LBA
// 读写持久化数据, 与引导方式无关。make_uefi_disk.py 构造 ESP 时已把这些簇保留为未分配。
//
// 诊断: 全程通过 COM1(0x3F8) 输出到 VMware 串口文件 vmware-uefi-serial.log, 便于无头定位。

#include "efi.h"
#include "font8x8.h"     // UEFI 无 int10h 无法从 VGA ROM 取 8x8 字体, 内置一份填到 0xB0000

// ---- 构建常量 ----
// 内核入口线性地址。内核是扁平镜像: 加载基址 0x100000, 镜像开头是 Multiboot2 头
// (24B + 对齐), 真正的入口 _start 在其后 (nm kernel.exe => 0x100020)。
// 构建脚本 make_uefi_vm.ps1 会用 nm 解析 _start 后经 -DKERNEL_ENTRY 注入; 这里只是兜底默认值。
// ⚠️ 绝不能跳 0x100000: 那是 Multiboot2 头(数据), 首字节 0xd6 在 64 位是非法指令,
//    一跳即 #UD 重启 (曾表现为 "ExitBootServices OK 后立刻重启")。
#ifndef KERNEL_ENTRY
#define KERNEL_ENTRY    0x100020ULL
#endif
// 内核链接(加载)基址: 扁平镜像必须整段落在这里, 内部绝对地址才正确。
#define KERNEL_LOAD_BASE 0x100000ULL

// 内核暂存: 0x100000 未必总能由 AllocateAddress 拿下 (固件内存布局有随机性,
// 同一镜像也可能这次成功下次失败)。失败时先存到任意地址, 等 ExitBootServices 之后
// (非运行时内存全部归 OS) 再搬到 KERNEL_LOAD_BASE。
static void* g_kern_stage = 0;      // 暂存缓冲地址 (EfiLoaderData)
static UINTN g_kern_size  = 0;      // 内核字节数
static int   g_kern_needs_move = 0; // 1 = 需要在 ExitBootServices 之后搬运
#define KERNEL_FILENAME L"\\KERNEL.BIN"
#define KERNEL_MAGIC    0xe85250d6ULL   // multiboot2 魔数 (小端: d6 50 52 e8)

static EFI_SYSTEM_TABLE* gST = 0;
static EFI_BOOT_SERVICES* gBS = 0;

// ---- 串口调试输出 (COM1, 0x3F8) 供无头 VMware 串口日志诊断 ----
static void outb_port(uint16_t p, uint8_t v){ __asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p)); }
static void uart_putc(char c){
    if (c == '\n') uart_putc('\r');
    // 注意: VMware 的"文件型"虚拟串口 (serial0.fileType=file) 未必会置 LSR 的 THR 空位,
    // 若自旋等待该位会死循环(第一条日志都打不出)。直接写 THR, VMware 会缓冲到文件。
    outb_port(0x3F8, (uint8_t)c);
}
static void uart_puts(const char* s){ while (*s) uart_putc(*s++); }
static void uart_init(void){
    // VMware 虚拟 16550A: 必须初始化 LCR/波特率, 否则裸 outb 到 THR 不出字.
    outb_port(0x3F8+3, 0x80);     // LCR: DLAB=1
    outb_port(0x3F8+0, 0x01);     // 除数 0x0001 -> 115200 baud
    outb_port(0x3F8+1, 0x00);
    outb_port(0x3F8+3, 0x03);     // LCR: 8N1, DLAB=0
    outb_port(0x3F8+2, 0x07);     // FCR: 使能 FIFO, 清收发
    outb_port(0x3F8+4, 0x03);     // MCR: DTR + RTS
    uart_puts("[UEFI] uart initialized\n");
}
static void uart_puthex(uint64_t n){
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t d = (n >> i) & 0xF;
        uart_putc(d < 10 ? ('0' + d) : ('A' + d - 10));
    }
}

static void Print(const CHAR16* s) {
    if (!gST || !gST->ConOut) return;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* co = (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*)gST->ConOut;
    co->OutputString(co, (CHAR16*)s);
    // 镜像到串口, 方便无头 VMware 串口日志 (vmware-uefi-serial.log) 诊断
    const CHAR16* p = s;
    while (*p) { char c = (char)(*p & 0x7F); uart_putc(c); p++; }
}
static void PrintErr(const CHAR16* s) {
    Print(L"FSOS UEFI ERROR: ");
    Print(s);
    for (;;) { __asm__ volatile("hlt"); }
}

static int guid_eq(const EFI_GUID* a, const EFI_GUID* b) {
    return a->Data1 == b->Data1 && a->Data2 == b->Data2 && a->Data3 == b->Data3 &&
           a->Data4[0]==b->Data4[0] && a->Data4[1]==b->Data4[1] &&
           a->Data4[2]==b->Data4[2] && a->Data4[3]==b->Data4[3] &&
           a->Data4[4]==b->Data4[4] && a->Data4[5]==b->Data4[5] &&
           a->Data4[6]==b->Data4[6] && a->Data4[7]==b->Data4[7];
}
static void print_u32(UINT32 n) {
    CHAR16 num[16]; UINTN k = 0;
    if (n == 0) num[k++] = '0';
    else { CHAR16 t[16]; UINTN t2 = 0; UINT32 x = n; while (x) { t[t2++] = (CHAR16)('0' + (x % 10)); x /= 10; } while (t2) num[k++] = t[--t2]; }
    num[k] = 0; Print(num);
}

// ---- 控制寄存器访问 ----
static void set_cr3(uint64_t v) { __asm__ volatile("mov %0,%%cr3" :: "r"(v)); }

// ---- 取 GOP 线性帧缓冲信息, 交给内核做显示输出 ----
// UEFI 下屏幕只由 GOP 帧缓冲驱动: 实测 legacy VGA 的 0xB8000 文本 / mode 13h 图形
// 都不上屏, 连把 SVGA 切回 VGA 仿真也不行。所以必须在 ExitBootServices 之前取到
// 帧缓冲地址/分辨率/格式, 写到内核约定的 0x6400, 由内核呈现 (present) 画面。
static gop_info_t* g_gop_info = 0;

static void query_gop(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = 0;
    gBS->HandleProtocol(gST->ConsoleOutHandle,
                        (EFI_GUID*)&gEfiGraphicsOutputProtocolGuid, (void**)&gop);
    if (!gop) {
        // 回退: 固件未必把 GOP 装在 ConOut 句柄上 (不同固件/分辨率策略有别),
        // 枚举全部带 GraphicsOutput 协议的句柄取第一个可用者。
        UINTN n = 0;
        EFI_HANDLE* buf = 0;
        if (gBS->LocateHandleBuffer(1 /*ByProtocol*/, (EFI_GUID*)&gEfiGraphicsOutputProtocolGuid,
                                    0, &n, &buf) == EFI_SUCCESS && n && buf) {
            for (UINTN i = 0; i < n; i++) {
                void* g2 = 0;
                if (gBS->HandleProtocol(buf[i], (EFI_GUID*)&gEfiGraphicsOutputProtocolGuid,
                                        &g2) == EFI_SUCCESS && g2) { gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL*)g2; break; }
            }
            gBS->FreePool(buf);
        }
    }
    if (!gop || !gop->Mode || !gop->Mode->Info || !gop->Mode->FrameBufferBase) {
        uart_puts("[UEFI] GOP not available (display will stay black)\n");
        return;
    }
    gop_info_t* gi = (gop_info_t*)(uintptr_t)GOP_INFO_ADDR;
    gi->magic   = GOP_INFO_MAGIC;
    gi->width   = gop->Mode->Info->HorizontalResolution;
    gi->height  = gop->Mode->Info->VerticalResolution;
    gi->pitch   = gop->Mode->Info->PixelsPerScanLine * 4;    // 32bpp
    gi->bpp     = 32;
    gi->format  = gop->Mode->Info->PixelFormat;
    gi->fb_addr = (uint64_t)gop->Mode->FrameBufferBase;
    g_gop_info = gi;
    uart_puts("[UEFI] GOP ");
    uart_puthex(gi->width); uart_puts("x"); uart_puthex(gi->height);
    uart_puts(" fmt="); uart_puthex(gi->format);
    uart_puts(" fb="); uart_puthex(gi->fb_addr);
    uart_puts("\n");
}

// 从 ESP 文件系统读取内核文件到 0x100000, 返回内核字节数 (<=0 表示失败)
static INTN load_kernel_from_fs(EFI_HANDLE ImageHandle) {
    EFI_STATUS st;
    void* li = 0;
    st = gBS->HandleProtocol(ImageHandle, (EFI_GUID*)&gEfiLoadedImageProtocolGuid, &li);
    if (st != EFI_SUCCESS || !li) PrintErr(L"LoadedImage protocol missing\r\n");

    EFI_HANDLE dev = ((EFI_LOADED_IMAGE_PROTOCOL*)li)->DeviceHandle;
    void* fsraw = 0;
    st = gBS->HandleProtocol(dev, (EFI_GUID*)&gEfiSimpleFileSystemProtocolGuid, &fsraw);
    if (st != EFI_SUCCESS || !fsraw) PrintErr(L"SimpleFileSystem not on boot device\r\n");

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*)fsraw;
    void* rootraw = 0;
    st = fs->OpenVolume(fs, &rootraw);
    if (st != EFI_SUCCESS || !rootraw) PrintErr(L"OpenVolume failed\r\n");
    EFI_FILE_PROTOCOL* root = (EFI_FILE_PROTOCOL*)rootraw;
    uart_puts("[UEFI] ESP volume opened\n");

    void* kfileraw = 0;
    st = root->Open(root, &kfileraw, (CHAR16*)KERNEL_FILENAME, EFI_FILE_MODE_READ, 0);
    if (st != EFI_SUCCESS || !kfileraw) PrintErr(L"open \\KERNEL.BIN failed\r\n");
    EFI_FILE_PROTOCOL* kfile = (EFI_FILE_PROTOCOL*)kfileraw;
    uart_puts("[UEFI] KERNEL.BIN opened\n");

    // 用 GetInfo 取文件大小
    UINTN infoSize = sizeof(EFI_FILE_INFO) + 256;
    char infoBuf[sizeof(EFI_FILE_INFO) + 256];
    st = kfile->GetInfo(kfile, (EFI_GUID*)&gEfiFileInfoGuid, &infoSize, infoBuf);
    UINT32 ksize = 0;
    if (st == EFI_SUCCESS) {
        ksize = (UINT32)((EFI_FILE_INFO*)infoBuf)->FileSize;
    } else {
        ksize = 0;   // 退化: 直接读到 EOF
    }
    Print(L"KERNEL.BIN: ");
    print_u32(ksize);
    Print(L" bytes\r\n");
    uart_puts("[UEFI] kernel size known\n");

    if (ksize == 0 || ksize > 32*1024*1024) PrintErr(L"bad kernel size\r\n");

    // 先拿一块任意地址的暂存缓冲读入内核。不能依赖 AllocateAddress 直接占住
    // 0x100000: 固件内存布局存在随机性, 同一镜像也可能这次成功下次失败。
    UINTN kpages = (ksize + 0xFFF) / 0x1000 + 1;
    EFI_PHYSICAL_ADDRESS stage_pa = 0;
    st = gBS->AllocatePages(0 /*AllocateAnyPages*/, EfiLoaderData, kpages, &stage_pa);
    if (st != EFI_SUCCESS || !stage_pa) PrintErr(L"alloc kernel staging buffer failed\r\n");
    char* stage = (char*)(UINTN)stage_pa;

    UINTN remain = ksize;
    char* dst = stage;
    while (remain > 0) {
        UINTN chunk = remain;
        st = kfile->Read(kfile, &chunk, dst);
        if (st != EFI_SUCCESS) PrintErr(L"read KERNEL.BIN failed\r\n");
        if (chunk == 0) break;   // EOF (防御)
        dst += chunk;
        remain -= chunk;
    }
    kfile->Close(kfile);
    root->Close(root);
    uart_puts("[UEFI] kernel read into staging buffer\n");

    // 校验 multiboot2 魔数, 确认内核被正确读入 (防止 FAT 读错导致跳到垃圾)
    uint32_t* m = (uint32_t*)stage;
    if (*m != (uint32_t)KERNEL_MAGIC) {
        uart_puts("[UEFI] WARN: kernel magic mismatch! got ");
        uart_puthex((uint64_t)(*m));
        uart_puts("\n");
    } else {
        uart_puts("[UEFI] kernel magic OK (multiboot2)\n");
    }

    // 尝试直接把内核放到链接地址 0x100000
    EFI_PHYSICAL_ADDRESS kaddr = KERNEL_LOAD_BASE;
    st = gBS->AllocatePages(AllocateAddress, EfiLoaderCode, kpages, &kaddr);
    if (st == EFI_SUCCESS) {
        char* kd = (char*)(UINTN)KERNEL_LOAD_BASE;
        for (UINTN i = 0; i < ksize; i++) kd[i] = stage[i];
        gBS->FreePages(stage_pa, kpages);
        g_kern_needs_move = 0;
        uart_puts("[UEFI] kernel placed @0x100000 (AllocateAddress)\n");
    } else {
        // 0x100000 被固件占用 (Boot Services 数据), 退出后再搬。
        g_kern_stage = stage;
        g_kern_size  = ksize;
        g_kern_needs_move = 1;
        uart_puts("[UEFI] 0x100000 occupied; relocate after ExitBootServices\n");
    }
    return (INTN)ksize;
}

// 加载器自带 64 位 GDT (与 BIOS loader 的 gdt64 完全一致: 0x08 code / 0x10 data)
static uint64_t g_gdt[4] __attribute__((aligned(16))) = {
    0x0000000000000000ULL,    // 0: null
    0x0020980000000000ULL,    // 0x08: 64-bit code (L=1, D=0)
    0x0000920000000000ULL,    // 0x10: 64-bit data
    0x0000920000000000ULL,    // 0x18: 64-bit data (备用)
};

// 远跳刷新 CS, 使其指向自有 GDT 的 0x08 (64 位代码段)。
// 必须做: 内核的 idt_init 只做 lidt, 既不 lgdt 也不重载 CS, 完全依赖引导器留下 CS=0x08
// (BIOS loader 是 "jmp 0x08:pm64_start" 进长模式后才跳内核的)。若 UEFI 下沿用固件的
// CS (如 0x38), 那么第一个中断返回 (iretq) 会用它去查 (内核视角的) GDT -> 非法选择子
// -> #GP, 表现为 pit 初始化即 "general protection" 崩溃。
static void reload_cs(void) {
    __asm__ volatile(
        "pushq $0x08\n\t"                   // 目标 CS
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"                   // 目标 RIP
        "lretq\n\t"                         // 远返回 == 远跳, 刷新 CS
        "1:\n\t"
        : : : "rax", "memory");
}

// ---- 安装 8x8 字体到 0xB0000 (内核 vga/gfx 按 font[ch*8] 读字形) ----
// BIOS 路径由 loader.asm 用 int10h (AX=1130) 从 VGA ROM 拷到 0xB0000; UEFI 没有
// int10h 也没有 legacy VGA ROM, 只能内置一份同样布局的字体 (font8x8.h)。
static void install_font(void) {
    const uint8_t* font = (const uint8_t*)g_font8x8;   // [96][8] 平铺成 768 字节
    uint8_t* dst = (uint8_t*)(uintptr_t)0xB0000UL;
    for (int i = 0; i < 0x20 * 8; i++) dst[i] = 0;      // 控制字符区 (0x00~0x1F) 置空
    for (int i = 0; i < 96 * 8; i++) dst[0x20 * 8 + i] = font[i];  // 0x20~0x7F
    uart_puts("[UEFI] 8x8 font installed @0xB0000\n");
}

static void load_segments(void) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr;
    gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    gdtr.base  = (uint64_t)(UINTN)g_gdt;
    __asm__ volatile("lgdt %0" :: "m"(gdtr));
    uint16_t sel = 0x10;
    __asm__ volatile(
        "mov %0, %%ds\n\t"
        "mov %0, %%es\n\t"
        "mov %0, %%ss\n\t"
        "mov %0, %%fs\n\t"
        "mov %0, %%gs\n\t"
        :: "r"(sel) : "memory");
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    uart_init();                       // 必须先初始化 COM1, 否则后续串口日志全丢
    Print(L"FSOS UEFI Boot Loader\r\n");
    uart_puts("[UEFI] efi_main start\n");

    // 0) 取 GOP 帧缓冲信息 (必须在 ExitBootServices 之前; 之后内核只能靠它出画面)
    query_gop();

    // 1) 从 ESP 文件系统按文件名加载内核 (OS 文件式引导)
    INTN ksize = load_kernel_from_fs(ImageHandle);
    if (ksize <= 0) PrintErr(L"load KERNEL.BIN failed\r\n");

    // 2) 分配页表 (7 页: PML4 + PDPT + PD0..PD3 + 低 2MB 的 4KB PT), 一致映射 0~4GB
    EFI_PHYSICAL_ADDRESS pt = 0;
    EFI_STATUS s2 = gBS->AllocatePages(0 /*AllocateAnyPages*/, EfiLoaderData, 7, &pt);
    if (s2 != EFI_SUCCESS) PrintErr(L"alloc pagetable failed\r\n");
    uart_puts("[UEFI] pagetable allocated @ ");
    uart_puthex((uint64_t)(UINTN)pt);
    uart_puts("\n");
    uint64_t* pml4 = (uint64_t*)(UINTN)pt;
    uint64_t* pdpt = (uint64_t*)(UINTN)(pt + 0x1000);
    uint64_t* pd[4];
    for (int d = 0; d < 4; d++) pd[d] = (uint64_t*)(UINTN)(pt + 0x2000 + (UINTN)d * 0x1000);
    uint64_t* ptlow = (uint64_t*)(UINTN)(pt + 0x6000);   // 0~2MB 的 4KB 页表
    for (int d = 0; d < 4; d++) {
        for (int j = 0; j < 512; j++) { pml4[j]=0; pdpt[j]=0; pd[d][j]=0; }
    }
    for (int j = 0; j < 512; j++) ptlow[j] = 0;
    pml4[0] = (uint64_t)(UINTN)pdpt | 3;
    pdpt[0] = (uint64_t)(UINTN)pd[0] | 3;
    pdpt[1] = (uint64_t)(UINTN)pd[1] | 3;
    pdpt[2] = (uint64_t)(UINTN)pd[2] | 3;
    pdpt[3] = (uint64_t)(UINTN)pd[3] | 3;
    // 低 2MB 用 4KB 页 (保持 WB 缓存)。注意: UEFI 下 0xA0000 已不再连接屏幕, 内核
    // 只把它当作 320x200x8 的影子缓冲 (见 GOP 呈现), 因此按普通内存 (WB) 映射即可;
    // 真正需要 UC 的是 GOP 线性帧缓冲 (下方的 MMIO 处理)。
    for (int j = 0; j < 512; j++) ptlow[j] = ((uint64_t)j << 12) | 0x83ULL;
    pd[0][0] = (uint64_t)(UINTN)ptlow | 3;   // 指向 4KB PT (不能置 PS 位)
    for (int d = 0; d < 4; d++) {
        for (int j = 0; j < 512; j++) {
            if (d == 0 && j == 0) continue;              // 已改为指向 4KB PT
            // 虚拟地址 = d*1GB + j*2MB; 要构成"一致映射", 物理地址必须与之一一对应。
            // ⚠️ 原写法漏了 d*1GB 这一项, 结果 4 个 PD 全都映射 0~1GB —— 1GB 以上的
            // 地址 (PCI MMIO 如 SVGA 的 0xF0000000、本地 APIC 0xFEE00000、IO APIC
            // 0xFEC00000) 会被翻译到错误的物理地址, 表现为 MMIO 读回 0xFFFFFFFF。
            pd[d][j] = (((uint64_t)d << 30) + ((uint64_t)j << 21)) | 0x83; // P+RW+PS(2MB)
        }
    }
    // GOP 线性帧缓冲是 PCI MMIO: 必须映射为 UC (PCD), 否则内核的绘制写进 CPU 缓存
    // 而不送到显卡 -> 屏幕保持黑屏。
    if (g_gop_info && g_gop_info->magic == GOP_INFO_MAGIC && g_gop_info->fb_addr) {
        uint64_t fb = g_gop_info->fb_addr;
        uint64_t fbsz = (uint64_t)g_gop_info->pitch * g_gop_info->height;
        for (int d = 0; d < 4; d++) {
            for (int j = 0; j < 512; j++) {
                uint64_t va = ((uint64_t)d << 30) + ((uint64_t)j << 21);
                if (va + 0x200000ULL > fb && va < fb + fbsz) {
                    pd[d][j] = va | 0x93;   // P+RW+PCD (UC)
                }
            }
        }
        uart_puts("[UEFI] GOP framebuffer mapped uncached\n");
    }

    // 3) 获取内存映射 (为 ExitBootServices 准备 MapKey)
    // 关键: GetMemoryMap 的 DescriptorSize(dsize) 必须先取正确值。最可靠方式: 第一次用
    // NULL 缓冲调用, 固件会把正确的 descriptor 大小写入 dsize。若 dsize=0 直接调用会令
    // GetMemoryMap 永远返回 BUFFER_TOO_SMALL (无法放入任何描述符) —— 这是先前 ExitBootServices
    // 持续失败的根因。
    UINTN mmap_size = 0;
    UINTN map_key = 0, dsize = 0;
    UINT32 dver = 0;
    gBS->GetMemoryMap(&mmap_size, 0, &map_key, &dsize, &dver);  // 取 dsize (descriptor 大小)
    if (dsize < 40) dsize = 48;   // 兜底: 标准 EFI_MEMORY_DESCRIPTOR 大小
    // 分配 256KB 页对齐缓冲 (远超任何合理内存映射规模)
    EFI_PHYSICAL_ADDRESS mmap_pa = 0;
    gBS->AllocatePages(0 /*AllocateAnyPages*/, EfiLoaderData, 64, &mmap_pa);
    void* mmap = (void*)(UINTN)mmap_pa;
    mmap_size = 256 * 1024;       // 传入缓冲实际容量 (而非"所需大小"), 保证不会超
    gBS->GetMemoryMap(&mmap_size, mmap, &map_key, &dsize, &dver);
    uart_puts("[UEFI] memory map obtained\n");

    // 4) 关中断 -> 退出 Boot Services -> 切换页表 -> 重载段 -> 跳内核
    // 先清除固件在 GOP 帧缓冲上的文本 (启动横幅/光标), 否则内核 GUI 会"浮在"残留文字上
    if (gST && gST->ConOut) {
        ((EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*)gST->ConOut)->ClearScreen(gST->ConOut);
        uart_puts("[UEFI] console cleared\n");
    }
    uart_puts("[UEFI] before ExitBootServices\n");
    __asm__ volatile("cli");
    // 标准做法: 在循环里反复 GetMemoryMap + ExitBootServices, 直到成功。每次 GetMemoryMap
    // 都刷新 MapKey (应对固件定时器/事件在两次调用之间改动内存映射), ExitBootServices 用
    // 最新 MapKey, 因此重试即可成功。(不提升 TPL: 某些 EFI 在 TPL_NOTIFY 上调用
    // ExitBootServices 会返回 UNSUPPORTED。)
    EFI_STATUS st = EFI_SUCCESS + 1;   // 初值取非零, 进入循环
    for (int attempt = 0; attempt < 100 && st != EFI_SUCCESS; attempt++) {
        st = gBS->GetMemoryMap(&mmap_size, mmap, &map_key, &dsize, &dver);
        if (st == (EFI_STATUS)0x8000000000000005ULL) {  // BUFFER_TOO_SMALL: 扩页缓冲重试
            gBS->FreePages(mmap_pa, 64);
            mmap_size += 0x4000;
            gBS->AllocatePages(0 /*AllocateAnyPages*/, EfiLoaderData, 64 + (mmap_size >> 12), &mmap_pa);
            mmap = (void*)(UINTN)mmap_pa;
            st = EFI_SUCCESS + 1;
            continue;
        }
        if (st != EFI_SUCCESS) continue;
        st = gBS->ExitBootServices(ImageHandle, map_key);
    }
    if (st != EFI_SUCCESS) {
        uart_puts("[UEFI] ExitBootServices FAILED, status=");
        uart_puthex((uint64_t)(UINTN)st);
        uart_puts("\n");
        PrintErr(L"ExitBootServices failed\r\n");   // 内部 hlt
    }
    uart_puts("[UEFI] ExitBootServices OK\n");

    // 若 0x100000 早前被固件 (Boot Services) 占用, 此时已退出, 非运行时内存全部归 OS,
    // 可以安全地把内核从暂存缓冲搬到链接地址。注意: 此处不能再调用 Boot Services。
    if (g_kern_needs_move) {
        char* src = (char*)g_kern_stage;
        char* d   = (char*)(UINTN)KERNEL_LOAD_BASE;
        UINTN n   = g_kern_size;
        uart_puts("[UEFI] reloc src="); uart_puthex((uint64_t)(UINTN)src);
        uart_puts(" dst="); uart_puthex((uint64_t)(UINTN)d);
        uart_puts(" n="); uart_puthex((uint64_t)n); uart_puts("\n");
        if ((UINTN)src < (UINTN)d && (UINTN)src + n > (UINTN)d) {
            for (UINTN i = n; i > 0; i--) d[i - 1] = src[i - 1];  // 重叠且 src<dst: 反向拷贝
        } else {
            for (UINTN i = 0; i < n; i++) d[i] = src[i];
        }
        uart_puts("[UEFI] kernel relocated to 0x100000\n");
    }

    set_cr3((uint64_t)(UINTN)pml4);
    uart_puts("[UEFI] cr3 switched to own pagetable (identity 0~4GB)\n");

    load_segments();                  // 对齐 BIOS loader: ds/es/ss=0x10 平坦段
    uart_puts("[UEFI] segments reloaded\n");

    reload_cs();                      // 对齐 BIOS loader: CS=0x08 (自有 GDT 的 64 位代码段)
    uart_puts("[UEFI] cs reloaded to 0x08\n");

    install_font();                   // 填 8x8 字体到 0xB0000 (BIOS 路径由 int10h 干这事)

    uart_puts("[UEFI] jmp kernel @0x");
    uart_puthex((uint64_t)KERNEL_ENTRY);
    uart_puts("\n");
    // 用寄存器间接跳转 (不压返回地址), 与 BIOS loader 的 jmp rax 完全一致
    __asm__ volatile("jmp *%0" :: "r"(KERNEL_ENTRY));

    uart_puts("[UEFI] RETURNED from kernel (UNEXPECTED)\n"); // 正常情况下内核不会返回
    for (;;) { __asm__ volatile("hlt"); }
    return EFI_SUCCESS;
}
