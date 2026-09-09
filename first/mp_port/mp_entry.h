// mp_entry.h - 内核侧接口: 进入 MicroPython 运行时
// 由 terminal.c 调用。具体实现有两份:
//   - mp_stub.c        : 未启用 MicroPython 时的占位实现
//   - micropython/ports/fsos/mp_entry.c : 真实运行时
// 构建系统保证二者只链接其一。
#ifndef MP_ENTRY_H
#define MP_ENTRY_H

// 运行模式
#define MP_MODE_REPL 0   // 交互式 Python REPL
#define MP_MODE_BT   1   // 运行移植后的 "Better terminal" 应用

// 进入 MicroPython 运行时。此函数不返回(除非用户退出 REPL/应用)
// 返回 1 表示 Better terminal / REPL 已正常运行; 返回 0 表示该模式未能启动
// (例如 BT 字节码编译失败), 调用方应回退到内置 C 终端, 避免终端变成死入口。
int mp_fsos_run(int mode);

// 执行一段 Python 源码字符串 (供"开发"应用运行 .py 文件)。
// 与 mp_fsos_run(MP_MODE_REPL) 的区别: 后者是交互式 REPL, 不执行外部源码。
// 返回 0 表示正常执行完毕 (源码内异常由 MicroPython 自行打印)。
int mp_fsos_run_str(const char* src);

// 本内核是否编译进了真正的 MicroPython 运行时。
//   真实实现 (micropython/ports/fsos/mp_entry.c) -> 1
//   占位实现 (mp_stub.c, 未启用 MicroPython 的构建)      -> 0
// 终端据此决定: 可用则把系统终端交给 Better terminal, 否则回退到内置 C 终端,
// 保证任何构建配置下"终端"都不会变成一个不可用的死入口。
int mp_available(void);

// 桌面启动阶段调用: 预先初始化运行时并编译 Better terminal 字节码,
// 使首次打开终端也瞬时 (无需现场编译而"卡一下")。
// 真实实现会初始化运行时 + 编译一次; 占位实现为空操作。可安全多次调用。
void mp_fsos_prefetch(void);

#endif // MP_ENTRY_H
