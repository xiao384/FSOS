// module.h - 内核侧解释器模块加载器接口
//
// 设计: 仅当 lang_launch 请求 "C/C++" 或 "Java" 时, 才从磁盘专用 LBA 把对应解释器
// 镜像读入预留高地址区执行; 执行结束后将该区清零释放。空闲时内核不含任何解释器
// 代码/数据 —— 真正零占用。
#ifndef MODULE_H
#define MODULE_H
#include <stdint.h>
#include "layout.h"   // LBA_MOD_CINT / LBA_MOD_JVM / MOD_REGION_SECTORS

// 预留加载虚拟地址 (位于内核堆之上、0..4GB 恒等映射区内):
//   CINT 窗口 0x40000000..0x80000000 (1GB)
//   JVM  窗口 0x80000000..0xC0000000  (1GB)
// 该区由 loader 以 2MB 大页恒等映射, 内核(CPL0)可直接读入并跳入执行。
#define MOD_CINT_VA   0x40000000ULL
#define MOD_JVM_VA    0x80000000ULL
// 每模块窗口最大扇区数 (1GB); 内核按头部 size 精确读取/清零, 仅作上界保护
#define MOD_SECTORS   2097152
// 模块镜像在磁盘上的 LBA (UEFI/IDE 共用, 与 layout.h / make_uefi_disk.py 保持一致)
// 必须落在 ESP(UEFI: 34..131105) 与磁盘末尾之间, 且各区域互不重叠。
#define MOD_CINT_LBA  LBA_MOD_CINT   // 4000000 (与 layout.h LBA_MOD_CINT 一致)
#define MOD_JVM_LBA   LBA_MOD_JVM    // 136000

// 按语言名加载并运行对应模块。which = "C/C++" 或 "Java"。
// 返回 0 成功, 负值失败 (模块缺失/损坏/运行错误)。
int mod_load_run(const char* which, const char* src, const char* proc_name);

// 控制台输出 (供模块 syscall 调用): 写入串口镜像 + 滚动缓冲 (GUI 终端窗口后续渲染)
void console_emit(char c);
// 清空模块/解释器输出缓冲（一次程序运行前调用）
void console_clear(void);
// 取滚动缓冲最近 len 字节 (供 GUI 终端窗口/IDE 输出面板渲染), 返回拷贝长度
int  console_drain(char* dst, int len);

#endif // MODULE_H
