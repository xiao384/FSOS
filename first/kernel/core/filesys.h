// filesys.h - FSOS 扁平文件区 (内核层 API)
//
// 磁盘布局 (见 layout.h):
//   目录: LBA_FS_DIR  (1 扇区, 16 个目录项)
//   数据: LBA_FS_DATA (128 扇区, 每文件固定 8 扇区 = 4KB)
//
// 历史: 这套实现原先是 MicroPython 端口 (krn_bridge.c) 里的 static 代码,
// 只有 Python 能用; 内核 C 侧 (桌面应用/编辑器) 拿不到文件。这里提到内核层,
// **磁盘格式保持逐字节一致**, 因此旧磁盘上已存的文件仍然可读可写。
//
// 适用: 纯文本工程文件 (.py / .c / .java / .txt ...), 单文件 <= 4KB,
// 最多 16 个文件 —— 这是 320x200 教学内核的合理容量。
#ifndef FILESYS_H
#define FILESYS_H

#include <stdint.h>

#define FS_NAME_SZ   24     // 文件名最大长度 (含 NUL)
#define FS_MAX_FILES 64     // 目录项上限 (FS_MAX_FILES*32 字节 = FS_DIR_SECS*512)
#define FS_MAX_SIZE  16384  // 单文件最大字节数 (FS_FILE_SECS=32 扇区 = 16KB)
#define FS_DIR_SECS  4      // 目录占用扇区数 (64*32/512 = 4)
#define FS_FILE_SECS 32     // 单文件占用扇区上限 (16KB)
#define FS_DATA_SECS (FS_MAX_FILES * FS_FILE_SECS)  // 数据区总扇区数

// 目录项类型 (写入磁盘的 type 字段, 旧盘默认 0=文件, 向上兼容)
#define FS_TYPE_FILE 0
#define FS_TYPE_DIR  1

// 重新读取目录扇区 (外部写过盘后调用以刷新缓存)
void fs_init(void);

// 已用文件数 / 列出文件名 (names 至少 max 项, 每项 FS_NAME_SZ 字节), 返回数量
int  fs_count(void);
int  fs_list(char names[][FS_NAME_SZ], int max);

// 读文件到 buf (自动补 NUL), 返回字节数; <0 表示文件不存在或读盘失败
int  fs_read(const char* name, char* buf, int cap);
// 写文件 (覆盖或新建), 0 成功; <0 失败 (-3 目录满, -4 数据区满)
int  fs_write(const char* name, const char* data);
// 删除文件 (释放目录项), 0 成功
int  fs_remove(const char* name);
// 重命名 (文件或目录), 0 成功; <0 失败 (-1 不存在, -3 目标名已存在)
int  fs_rename(const char* old, const char* new);
// 文件字节数; <0 表示不存在
int  fs_size(const char* name);
// 统计: 已用目录项数 / 已用数据扇区数; 入参可为 NULL
int  fs_stats(int* used_entries, int* used_sectors);

// ---- 目录 (文件夹) 支持: 所有 *_in 操作都针对某个目录块 (dir_lba) ----
//     根目录 dir_lba = LBA_FS_DIR; 子目录 dir_lba = 其目录块所在 LBA。
//     同一份数据区被所有目录共享分配, 因此可嵌套任意层子目录。
int  fs_list_in(uint32_t dir_lba, char names[][FS_NAME_SZ], uint8_t types[], int max);
int  fs_read_in(uint32_t dir_lba, const char* name, char* buf, int cap);
int  fs_write_in(uint32_t dir_lba, const char* name, const char* data);
int  fs_remove_in(uint32_t dir_lba, const char* name);
int  fs_rename_in(uint32_t dir_lba, const char* old, const char* new);
int  fs_size_in(uint32_t dir_lba, const char* name);
int  fs_stats_in(uint32_t dir_lba, int* used_entries, int* used_sectors);
// 在 dir_lba 下新建子目录, 0 成功; <0 失败 (-1 重名, -4 数据区满)
int  fs_mkdir(uint32_t dir_lba, const char* name);
// 删除空子目录, 0 成功; <0 失败 (-1 不存在, -2 非空)
int  fs_rmdir(uint32_t dir_lba, const char* name);
// 取得子目录块 LBA (供进入导航); 非目录/不存在返回 0
uint32_t fs_subdir_lba(uint32_t dir_lba, const char* name);
// 整盘统计 (含所有子目录), 用于显示 FS 真实占用
int  fs_stats_total(int* used_entries, int* used_sectors);
// 格式化文件系统: 清空根目录 (所有文件/子目录丢失), 数据区不擦除
void fs_format(void);

#endif // FILESYS_H
