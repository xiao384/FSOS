// terminal.h - 系统终端 (Shell), 默认 root(最高)权限
#ifndef TERMINAL_H
#define TERMINAL_H

// 运行终端 (阻塞直到输入 exit / 按 ESC 返回桌面)
// 无论登录用户角色如何, 终端会话始终以 root 最高权限执行
void terminal_run(void);

// 执行一条命令 (供 krn.run() 等复用), 等价于终端输入该行
void run_command(const char* line);

// root 运行时注册自定义命令 (name 唯一, line 为绑定的终端命令字符串 => 用户宏)
// 返回 0 成功, -1 重名/非法/表满。注册的命令属"非核心"扩展, 不改动内核。
// 例: term_register_cmd("hi", "echo hello") 之后在终端输入 hi 即执行 echo hello。
int term_register_cmd(const char* name, const char* line);

// 遍历当前命令表 (供 krn.list_cmds() 等使用)
int         term_cmd_count(void);
const char* term_cmd_name(int i);

#endif // TERMINAL_H
