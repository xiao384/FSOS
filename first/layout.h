// layout.h - 磁盘布局唯一权威定义
//
// 重要: 所有持久化区域必须集中在此定义, 禁止在各模块内硬编码 LBA。
//
// 历史教训: 用户库曾硬编码在 LBA 64/65、Python 文件系统在 LBA 300/320,
// 二者都落在内核镜像内部 (内核自 LBA 9 起), 于是"保存数据"就等于"覆盖内核代码":
//   首次启动 -> user_init()/write_file() 写盘 -> 内核代码被覆盖
//           -> 之后每次启动都读到坏内核 -> 随机 #UD/#PF/双故障
// 该故障极难定位 (表现为"偶发启动失败")。因此所有区域必须:
//   1) 位于内核之后 (LBA_KERNEL_END_MAX 之后)
//   2) 彼此不重叠
//   3) 由 build-mingw.ps1 在构建时校验
//
// 镜像容量 8388608 扇区 = 4GB (GB 级重布局: 内核/堆/模块窗口/数据统一映射于 0..4GB 恒等地址空间, VM 内存 4096MB 覆盖模块窗口), 布局如下:
//
//   LBA 0           引导扇区
//   LBA 1..8        二级引导 loader
//   LBA 9 ..       内核镜像 (最多 KERNEL_MAX_SECTORS 个扇区)
//   LBA 6000       用户库超级块
//   LBA 6001       用户库记录
//   LBA 6040       Python krn 文件系统目录
//   LBA 6060       krn 文件系统数据 (FS_DATA_SECS=2048 扇区 = 64 文件 x 32 扇区, 位于 SYSCONF 与模块间空闲区)
//   LBA 6020       系统配置区 (root 可修改: 主题/配色/启动项等)
//
#ifndef LAYOUT_H
#define LAYOUT_H

// ---- 镜像容量 ----
#define DISK_SECTORS        8388608     // 4GB (512B/扇区; LBA28 上限 2^28≈137GB 足够)

// ---- 引导 ----
#define LBA_BOOT            0
#define LBA_LOADER          1
#define LBA_KERNEL          9

// ---- 内核之后的数据区 (随内核镜像增大从 3800 迁移到 6000 起) ----
#define LBA_USER_SB         6000        // 用户库超级块
#define LBA_USER_REC        6001        // 用户库记录 (UserRec[16])
#define LBA_FS_DIR          6040        // krn 文件系统目录 (FS_DIR_SECS 扇区, 见 filesys.h)
#define LBA_SYSCONF         6020        // 系统配置区 (4 扇区)
#define LBA_FS_DATA         6060        // krn 文件系统数据 (FS_DATA_SECS 扇区, 位于 SYSCONF 与模块间空闲区)

// ---- 解释器模块 (按需从磁盘读入预留高地址窗口, 运行完即释放) ----
// 必须与 first/user/module.h 的 MOD_*_VA/MOD_SECTORS 及 first/tools/make_uefi_disk.py
// 的 MOD_*_LBA 完全一致。每模块窗口 1GB, 两模块共 2GB, 位于 4GB 磁盘高位避开 UEFI ESP(≤131105)。
#define LBA_MOD_CINT        4000000     // CINT.MOD
#define LBA_MOD_JVM         6097152     // JVM.MOD (= CINT + 2097152)
#define MOD_REGION_SECTORS  2097152     // 每模块保留扇区 (1GB)

// 内核可用扇区数上限 (构建脚本据此校验内核是否越界)
#define KERNEL_MAX_SECTORS  (LBA_USER_SB - LBA_KERNEL)   // 3791

#endif // LAYOUT_H
