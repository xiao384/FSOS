// mod_abi.h - 内核 <--> 解释器模块 的二进制接口 (ABI)
//
// 模块是独立编译、固定虚拟地址链接的裸 blob, 由内核在运行时从磁盘读入预留区执行。
// 模块禁止直接引用任何内核符号, 所有 OS 服务必须经本结构体提供的函数指针访问,
// 以保证"可加载 / 运行完即释放 / 空闲零占用"。
//
// 本头同时被内核(module.c)与模块源码(mod_*.c, 经 -I user 引入)包含, 故必须自包含
// (仅依赖 <stdint.h>), 不得引入内核头。
#ifndef MOD_ABI_H
#define MOD_ABI_H
#include <stdint.h>
#include <stddef.h>          // size_t (malloc 字段签名用)

#define MOD_MAGIC  0x4F534D31ULL   // 'OSM1'
#define MOD_ABI_VERSION 2          // mod_header_t.version 当前版本
#define MOD_HDRSZ  32              // mod_header_t 字节数 (32 字节, 8 字节对齐)

struct mod_syscalls;              // 前向声明 (mod_entry_t 用到, 定义见下方)

// blob 头部: 必须位于文件最前 (链接脚本 .modhdr 段, 偏移 0)
//   magic     = MOD_MAGIC ('OSM1', 小端 0x4F534D31)
//   version   = MOD_ABI_VERSION (ABI 不兼容时内核拒绝加载)
//   entry_off = module_entry 相对 blob 基址的偏移 (恒为 MOD_HDRSZ)
//   size      = 整个 blob 字节数 (含头部); 由构建脚本在 objcopy 后回填,
//               内核据此精确读取扇区数, 0 表示"未知 -> 读取最大 MOD_SECTORS"
//   crc32     = [MOD_HDRSZ, size) 的 IEEE CRC32; 0 表示跳过校验
//   flags     = 保留
typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t entry_off;
    uint32_t size;
    uint32_t crc32;
    uint32_t flags;
    uint32_t reserved[1];
} __attribute__((packed)) mod_header_t;

// 模块入口签名: 返回 0 成功。
//   src       = 用户源码 (NULL 表示运行内置示例)
//   proc_name = 进程/文件名 (用于标题)
typedef int (*mod_entry_t)(const struct mod_syscalls* sc,
                           const char* src, const char* proc_name);

// 内核提供给模块的服务表 (函数指针, 模块不得假设其绝对地址)
typedef struct mod_syscalls {
    void* (*malloc)(size_t n);
    void  (*free)(void* p);
    void  (*putc)(char c);                       // 输出一个字符到当前控制台
    void  (*puts)(const char* s);                // 输出 C 字符串
    int   (*file_read)(const char* name, char* buf, int cap);
    int   (*file_write)(const char* name, const char* data);
    int   (*file_list)(char names[][24], int max);
    int   (*file_exists)(const char* name);
    void  (*log)(const char* s);                 // 调试日志 (串口)
    uint64_t (*tick_ms)(void);                   // 毫秒计数
    int   (*poll)(void);                         // 执行期间的取消/超时检查；<0=终止
} mod_syscalls_t;

#endif // MOD_ABI_H
