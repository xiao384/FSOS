// mpconfigport.h - FSOS 上的 MicroPython 端口配置
// 参考 micropython/ports/minimal/mpconfigport.h, 适配裸机 i686 + 本项目内核 HAL.
#ifndef MICROPY_MPORTCONFIG_H
#define MICROPY_MPORTCONFIG_H

#include <stdint.h>

// ---- 类型定义 (必须与 py/ 期望一致) ----
typedef intptr_t mp_int_t;   // 必须是指针宽度
typedef uintptr_t mp_uint_t;  // 必须是指针宽度
typedef long long mp_off_t;   // MinGW64 下 long 仅 32 位, 文件偏移用 64 位

// 裸机环境没有 <alloca.h>; alloca 用编译器内建
// (MinGW 宿主头文件可能已定义 alloca, 加保护避免重定义警告)
#ifndef alloca
#define alloca __builtin_alloca
#endif

// ---- MicroPython 功能配置 ----
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

// 启用内置编译器 (REPL 需要)
#define MICROPY_ENABLE_COMPILER (1)

// input() 在 ROM_LEVEL_MINIMUM 下默认禁用, 但 bt.py 的交互输入需要它
#define MICROPY_PY_BUILTINS_INPUT (1)

// BT(inflate.py/b64.py) 需要切片下标 a[b:c] 与 bytearray(); ROM_LEVEL_MINIMUM
// 默认把二者关掉, 会导致 BT 内嵌源码编译期 SyntaxError / 运行时 NameError。
// 这里显式开启 (objslice.c / objbytearray.c 体量很小, UEFI 路径不受 45.5KB 限制)。
#define MICROPY_PY_BUILTINS_SLICE (1)
#define MICROPY_PY_BUILTINS_BYTEARRAY (1)

// 注意: MICROPY_PY_BUILTINS_STR_OP_MODULO 不启用!
// 字符串 % 格式化 ("%s" % x) 的实现 (str_modulo_format) 约 44KB,
// 会把内核第二段撑到 154 扇区, 超过引导低端缓冲 (45.5KB) 硬上限。
// bt.py 已改为字符串拼接, 不依赖 % 格式化。

// 强制使用 setjmp 版非本地异常处理 (nlrsetjmp.c)
// 原因: MinGW 同时定义 _WIN32 与 __i386__/__x86_64__, 会误入原生 x86 NLR 的
//       Windows 分支 (nlrx64.s 使用 ELF 汇编, 无法在 PE 目标下汇编).
//       setjmp 版只需编译器内建, 用本项目 ports/fsos/setjmp.[ch].
#define MICROPY_NLR_SETJMP (1)

// 裸机环境不链接 libm, 关闭浮点与依赖浮点的模块 (bt.py 用不到)
#define MICROPY_PY_BUILTINS_FLOAT (0)
#define MICROPY_PY_MATH (0)
#define MICROPY_PY_CMATH (0)
#define MICROPY_PY_THREAD (0)

// 启用垃圾回收 (必需)
#define MICROPY_ENABLE_GC (1)

// 启用 REPL 辅助 (行编辑/历史)
#define MICROPY_HELPER_REPL (1)

// 不使用冻结 .mpy 模块 (我们用 mp_exec_str 直接执行内嵌源码)
#define MICROPY_MODULE_FROZEN_MPY (0)

// FSOS 有自己的扁平文件区，并在 mp_entry.c 实现 mp_import_stat /
// mp_lexer_new_from_file；开启外部 import 后，多文件 Python 应用可使用 import。
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)

// 路径/解析节点相关
#define MICROPY_ALLOC_PATH_MAX (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)

// 板子信息 (会反映在 sys.implementation 中)
#define MICROPY_HW_BOARD_NAME "FSOS"
#ifdef __x86_64__
#define MICROPY_HW_MCU_NAME   "x86-64"
#else
#define MICROPY_HW_MCU_NAME   "i686"
#endif

// 端口私有状态: 直接复用 VM 的全局状态槽
#define MP_STATE_PORT MP_STATE_VM

// ---- 堆 (GC) 位置 ----
// 内核加载在 1MB, 栈在其后 64KB (0x1a8000-0x1b8000)。
// 这里把 MicroPython 堆放在 16MB 处, 避开内核/栈/VGA, 且 QEMU 默认内存必达。
// (原 0x04000000=64MB 在某些 QEMU 配置/旧版本下可能越界, 故下移)
#ifndef MP_HEAP_START
#define MP_HEAP_START ((void *)0x01000000)  // 16 MB
#endif
#ifndef MP_HEAP_SIZE
#define MP_HEAP_SIZE  (0x00800000)          // 8 MB
#endif

#endif // MICROPY_MPORTCONFIG_H
