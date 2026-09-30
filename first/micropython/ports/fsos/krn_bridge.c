// krn_bridge.c - 暴露内核服务给 MicroPython 的 "krn" 模块
// 移植自 Better terminal_project(pt) 的命令逻辑, 但去掉 tkinter/桌面自动化,
// 改为调用本项目内核的 user / ata / 电源接口.
#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/builtin.h"
#include "py/mphal.h"
#include "py/mperrno.h"

#include "user.h"
#include "ata.h"
#include "io.h"
#include "sysconf.h"     // 持久化系统配置 (主题/主机名)
#include "terminal.h"   // run_command(): 复用终端命令分发
#include "driver.h"     // 系统驱动表 (krn.drivers/load_driver)
#include "vga.h"        // 内核 VGA 文本原语 (krn.term_*)
#include "kb.h"         // kb_poll(): 非阻塞键盘 (krn.kb_poll)
#include "mouse.h"      // mouse_get(): 鼠标状态 (krn.mouse)
#include "idt.h"        // get_ticks(): 毫秒计时 (krn.delay)
#include "filesys.h"    // 内核文件区 (与桌面"开发"应用共用同一份磁盘文件)
#include "power.h"      // 统一 ACPI 电源/复位服务

// ============================================================
// 文件层: 原先是本文件内的 static 实现, 已提到 kernel/core/filesys.c
// (磁盘格式逐字节不变), 这样桌面应用 (user/devstudio.c) 与 Python 可以
// 读写同一批工程文件, 而不是各存一份。
//   目录: 1 扇区, 最多 16 个文件
//   数据: 每文件最多 8 扇区 (4KB)
//
// 重要: 位置由 layout.h 统一给出, 必须位于内核镜像之后。
// 旧值 LBA 300/320 落在内核镜像内部 (内核自 LBA 9 起), 于是 write_file()
// 会把文件写进内核代码区 -> 内核被覆盖 -> 之后每次启动崩溃。
// ============================================================
// ============================================================
// krn 模块函数
// ============================================================
STATIC mp_obj_t krn_whoami(void) {
    return mp_obj_new_str("root", 4);  // 内核终端恒为 root
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_whoami_obj, krn_whoami);

STATIC mp_obj_t krn_message(void) {
    const char *msg =
        "FSOS v0.2 (x86-64 kernel)\r\n"
        "MicroPython on bare metal\r\n"
        "User: root (highest privilege)";
    return mp_obj_new_str(msg, strlen(msg));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_message_obj, krn_message);

// 返回 [[name, role_str], ...]
STATIC mp_obj_t krn_users(void) {
    int cnt = user_count();
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < cnt; i++) {
        const UserRec *u = user_get(i);
        if (!u) break;
        const char *role = (u->role == ROLE_ADMIN) ? "root" : "users";
        mp_obj_t pair[2];
        pair[0] = mp_obj_new_str(u->name, strlen(u->name));
        pair[1] = mp_obj_new_str(role, strlen(role));
        mp_obj_list_append(list, mp_obj_new_tuple(2, pair));
    }
    return list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_users_obj, krn_users);

STATIC mp_obj_t krn_user_add(size_t n_args, const mp_obj_t *args) {
    const char *name = mp_obj_str_get_str(args[0]);
    const char *pass = mp_obj_str_get_str(args[1]);
    const char *role = (n_args > 2) ? mp_obj_str_get_str(args[2]) : "users";
    uint8_t r = (role[0] == 'r' && role[1] == 'o') ? ROLE_ADMIN : ROLE_NORMAL;
    int rc = user_add(name, pass, r);
    if (rc == 0) user_save();
    return mp_obj_new_str(rc == 0 ? "user added" : "ERROR: add failed",
                          rc == 0 ? 10 : 16);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(krn_user_add_obj, 2, 3, krn_user_add);

STATIC mp_obj_t krn_user_del(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    int idx = user_find(name);
    if (idx < 0) return mp_obj_new_str("ERROR: user not found", 21);
    int rc = user_remove(idx);
    if (rc == 0) user_save();
    return mp_obj_new_str(rc == 0 ? "user deleted" : "ERROR: delete failed",
                          rc == 0 ? 13 : 21);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_user_del_obj, krn_user_del);

STATIC mp_obj_t krn_user_setpass(mp_obj_t name_in, mp_obj_t pass_in) {
    const char *name = mp_obj_str_get_str(name_in);
    const char *pass = mp_obj_str_get_str(pass_in);
    int idx = user_find(name);
    if (idx < 0) return mp_obj_new_str("ERROR: user not found", 21);
    user_setpass(idx, pass);
    user_save();
    return mp_obj_new_str("password changed", 16);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(krn_user_setpass_obj, krn_user_setpass);

STATIC mp_obj_t krn_user_setrole(mp_obj_t name_in, mp_obj_t role_in) {
    const char *name = mp_obj_str_get_str(name_in);
    const char *role = mp_obj_str_get_str(role_in);
    uint8_t r = (role[0] == 'r' && role[1] == 'o') ? ROLE_ADMIN : ROLE_NORMAL;
    int idx = user_find(name);
    if (idx < 0) return mp_obj_new_str("ERROR: user not found", 21);
    user_setrole(idx, r);
    user_save();
    return mp_obj_new_str("role updated", 13);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(krn_user_setrole_obj, krn_user_setrole);

STATIC mp_obj_t krn_reboot(void) {
    // 统一走 ACPI RESET_REG，失败后由更高层选择 8042 fallback。
    (void)reboot_system();
    outb(0x64, 0xFE);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_reboot_obj, krn_reboot);

STATIC mp_obj_t krn_poweroff(void) {
    // 与桌面/终端统一的 ACPI FADT + DSDT _S5_ 服务。
    // 失败时仅返回异常码给上层，不再把“CPU 停止”伪装成“系统关机”。
    (void)poweroff_system();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_poweroff_obj, krn_poweroff);

// 读文件: 返回字符串 (经内核文件区, 与桌面"开发"应用同一份数据)
STATIC mp_obj_t krn_read_file(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    char *buf = m_malloc(FS_MAX_SIZE + 1);
    if (!buf) mp_raise_OSError(MP_ENOMEM);
    int n = fs_read(path, buf, FS_MAX_SIZE + 1);
    if (n < 0) { m_free(buf); mp_raise_OSError(MP_ENOENT); }
    mp_obj_t s = mp_obj_new_str(buf, (size_t)n);
    m_free(buf);
    return s;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_read_file_obj, krn_read_file);

// 写文件 (最多 4KB)
STATIC mp_obj_t krn_write_file(mp_obj_t path_in, mp_obj_t data_in) {
    const char *path = mp_obj_str_get_str(path_in);
    size_t len;
    const char *data = mp_obj_str_get_data(data_in, &len);
    if (len > (size_t)FS_MAX_SIZE) len = (size_t)FS_MAX_SIZE;
    char *buf = m_malloc(len + 1);
    if (!buf) mp_raise_OSError(MP_ENOMEM);
    memcpy(buf, data, len);
    buf[len] = 0;
    int rc = fs_write(path, buf);
    m_free(buf);
    if (rc == 0) return mp_obj_new_str("file written", 12);
    if (rc == -3 || rc == -4) mp_raise_OSError(MP_ENOSPC);
    mp_raise_OSError(MP_EIO);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(krn_write_file_obj, krn_write_file);

// 列出文件区内的文件名 (原先 Python 侧只能靠 INDEX.TXT 间接列目录)
STATIC mp_obj_t krn_list_files(void) {
    static char names[FS_MAX_FILES][FS_NAME_SZ];
    fs_init();                                  // 重新读目录扇区, 看到 C 侧的写入
    int n = fs_list(names, FS_MAX_FILES);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < n; i++) {
        mp_obj_list_append(list, mp_obj_new_str(names[i], strlen(names[i])));
    }
    return list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_list_files_obj, krn_list_files);

// 删除文件 (释放目录项与数据槽位)
STATIC mp_obj_t krn_del_file(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    int rc = fs_remove(name);
    return mp_obj_new_str(rc == 0 ? "file deleted" : "ERROR: no such file",
                          rc == 0 ? 12 : 20);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_del_file_obj, krn_del_file);

// ============================================================
// 系统定制 (root 专属): 主题/主机名/配置/执行命令
// 这些接口让 root 通过 Python 脚本自由修改除内核核心以外的一切。
// 配置落盘于 LBA_SYSCONF (布局上位于内核之后), 与核心物理隔离,
// 因此修改配置/主题绝不会破坏内核。
// ============================================================

// 首 token 前缀匹配 (避免引入 libc 依赖)
static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

STATIC mp_obj_t krn_theme(mp_obj_t idx_in) {
    int idx = mp_obj_get_int(idx_in);
    if (idx < 0 || idx > 2) return mp_obj_new_str("ERROR: theme must be 0..2", 25);
    sysconf_load();
    sysconf()->theme = (uint8_t)idx;
    sysconf_save();
    return mp_obj_new_str("theme set (reboot to apply to GUI)", 33);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_theme_obj, krn_theme);

STATIC mp_obj_t krn_hostname(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    sysconf_load();
    sysconf_t *c = sysconf();
    int n = 0;
    while (name[n] && n < 15) { c->hostname[n] = name[n]; n++; }
    c->hostname[n] = '\0';
    sysconf_save();
    return mp_obj_new_str("hostname set", 13);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_hostname_obj, krn_hostname);

STATIC mp_obj_t krn_config(void) {
    sysconf_load();
    sysconf_t *c = sysconf();
    mp_obj_t dict = mp_obj_new_dict(0);
    mp_obj_dict_store(dict, mp_obj_new_str("theme", 5),       mp_obj_new_int(c->theme));
    mp_obj_dict_store(dict, mp_obj_new_str("wallpaper", 9),   mp_obj_new_int(c->wallpaper));
    mp_obj_dict_store(dict, mp_obj_new_str("hostname", 8),    mp_obj_new_str(c->hostname, strlen(c->hostname)));
    mp_obj_dict_store(dict, mp_obj_new_str("autostart", 9),   mp_obj_new_int(c->autostart));
    return dict;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_config_obj, krn_config);

// 执行任意终端命令 (root 可借此脚本化系统管理)
STATIC mp_obj_t krn_run(mp_obj_t line_in) {
    const char *line = mp_obj_str_get_str(line_in);
    if (starts_with(line, "python") || starts_with(line, "bt"))
        return mp_obj_new_str("ERROR: use 'exit' to leave REPL first", 38);
    run_command(line);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_run_obj, krn_run);

// ============================================================
// 系统驱动 (root 可查看/加载驱动, 但核心驱动受布局守卫保护不可覆盖)
// ============================================================
// 返回 [[name, type, status, builtin], ...]
STATIC mp_obj_t krn_drivers(void) {
    int n = drv_count();
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < n; i++) {
        driver_t *d = drv_get(i);
        if (!d) break;
        mp_obj_t row[4];
        row[0] = mp_obj_new_str(d->name, strlen(d->name));
        row[1] = mp_obj_new_str(drv_type_str(d->type), strlen(drv_type_str(d->type)));
        row[2] = mp_obj_new_str(drv_status_str(d->status), strlen(drv_status_str(d->status)));
        row[3] = mp_obj_new_bool(d->builtin);
        mp_obj_list_append(list, mp_obj_new_tuple(4, row));
    }
    return list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_drivers_obj, krn_drivers);

// 按名加载(初始化)一个驱动, 返回其状态字符串
STATIC mp_obj_t krn_load_driver(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    int st = drv_load(name);
    return mp_obj_new_str(drv_status_str(st), strlen(drv_status_str(st)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_load_driver_obj, krn_load_driver);

// 列出全部终端命令名
STATIC mp_obj_t krn_list_cmds(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    int n = term_cmd_count();
    for (int i = 0; i < n; i++) {
        const char *nm = term_cmd_name(i);
        if (nm) mp_obj_list_append(list, mp_obj_new_str(nm, strlen(nm)));
    }
    return list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_list_cmds_obj, krn_list_cmds);

// 注册自定义命令 (root 宏): 绑定一行终端命令
STATIC mp_obj_t krn_register_cmd(mp_obj_t name_in, mp_obj_t line_in) {
    const char *name = mp_obj_str_get_str(name_in);
    const char *line = mp_obj_str_get_str(line_in);
    int rc = term_register_cmd(name, line);
    return mp_obj_new_str(rc == 0 ? "command registered" : "ERROR: register failed",
                          rc == 0 ? 18 : 23);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(krn_register_cmd_obj, krn_register_cmd);

// ============================================================
// 内核终端绘制 / 输入接口 (供 Better terminal 自定义终端使用)
// ============================================================
STATIC mp_obj_t krn_term_clear(void) {
    vga_clear(COL_BLACK);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_term_clear_obj, krn_term_clear);

// term_text(x, y, text, color) -> 在像素 (x,y) 以 8x8 字模绘制文本
STATIC mp_obj_t krn_term_text(size_t n_args, const mp_obj_t *args) {
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    const char *text = mp_obj_str_get_str(args[2]);
    int color = mp_obj_get_int(args[3]);
    vga_draw_text(x, y, text, (uint8_t)color, COL_BLACK);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(krn_term_text_obj, 4, 4, krn_term_text);

// term_fill(x0, y0, x1, y1, color) -> 填充矩形 (鼠标指针/清行)
STATIC mp_obj_t krn_term_fill(size_t n_args, const mp_obj_t *args) {
    vga_fill_rect(mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
                  mp_obj_get_int(args[2]), mp_obj_get_int(args[3]),
                  (uint8_t)mp_obj_get_int(args[4]));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(krn_term_fill_obj, 5, 5, krn_term_fill);

// kb_poll() -> 0 表示无键, 否则为按键码 (13=Enter, 8=BS, 32..126=可打印)
STATIC mp_obj_t krn_kb_poll(void) {
    return mp_obj_new_int(kb_poll());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_kb_poll_obj, krn_kb_poll);

// mouse() -> (x, y, left, right, present)
STATIC mp_obj_t krn_mouse(void) {
    mouse_state_t m;
    mouse_get(&m);
    mp_obj_t t[5];
    t[0] = mp_obj_new_int(m.x);
    t[1] = mp_obj_new_int(m.y);
    t[2] = mp_obj_new_bool(m.left);
    t[3] = mp_obj_new_bool(m.right);
    t[4] = mp_obj_new_bool(m.present);
    return mp_obj_new_tuple(5, t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_mouse_obj, krn_mouse);

// screen_size() -> (width, height), 返回当前 GOP/LFB 的真实像素尺寸。
STATIC mp_obj_t krn_screen_size(void) {
    mp_obj_t row[2];
    row[0] = mp_obj_new_int(VGA_W);
    row[1] = mp_obj_new_int(SCREEN_H);
    return mp_obj_new_tuple(2, row);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(krn_screen_size_obj, krn_screen_size);

// delay(ms) -> 忙等若干毫秒 (避免空转)
STATIC mp_obj_t krn_delay(mp_obj_t ms_in) {
    uint32_t ms = (uint32_t)mp_obj_get_int(ms_in);
    uint32_t start = get_ticks();
    while (get_ticks() - start < ms) { __asm__ volatile("nop"); }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(krn_delay_obj, krn_delay);

// ============================================================
// 模块定义
// ============================================================
STATIC const mp_rom_map_elem_t krn_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_krn) },
    { MP_ROM_QSTR(MP_QSTR_whoami),       MP_ROM_PTR(&krn_whoami_obj) },
    { MP_ROM_QSTR(MP_QSTR_message),      MP_ROM_PTR(&krn_message_obj) },
    { MP_ROM_QSTR(MP_QSTR_users),        MP_ROM_PTR(&krn_users_obj) },
    { MP_ROM_QSTR(MP_QSTR_user_add),     MP_ROM_PTR(&krn_user_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_user_del),     MP_ROM_PTR(&krn_user_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_user_setpass), MP_ROM_PTR(&krn_user_setpass_obj) },
    { MP_ROM_QSTR(MP_QSTR_user_setrole), MP_ROM_PTR(&krn_user_setrole_obj) },
    { MP_ROM_QSTR(MP_QSTR_reboot),       MP_ROM_PTR(&krn_reboot_obj) },
    { MP_ROM_QSTR(MP_QSTR_poweroff),     MP_ROM_PTR(&krn_poweroff_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_file),    MP_ROM_PTR(&krn_read_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_file),   MP_ROM_PTR(&krn_write_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_list_files),   MP_ROM_PTR(&krn_list_files_obj) },
    { MP_ROM_QSTR(MP_QSTR_del_file),     MP_ROM_PTR(&krn_del_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_theme),        MP_ROM_PTR(&krn_theme_obj) },
    { MP_ROM_QSTR(MP_QSTR_hostname),     MP_ROM_PTR(&krn_hostname_obj) },
    { MP_ROM_QSTR(MP_QSTR_config),       MP_ROM_PTR(&krn_config_obj) },
    { MP_ROM_QSTR(MP_QSTR_run),          MP_ROM_PTR(&krn_run_obj) },
    { MP_ROM_QSTR(MP_QSTR_drivers),      MP_ROM_PTR(&krn_drivers_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_driver),  MP_ROM_PTR(&krn_load_driver_obj) },
    { MP_ROM_QSTR(MP_QSTR_list_cmds),    MP_ROM_PTR(&krn_list_cmds_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_cmd), MP_ROM_PTR(&krn_register_cmd_obj) },

    // 内核终端绘制 / 输入接口 (Better terminal 自定义终端用)
    { MP_ROM_QSTR(MP_QSTR_term_clear), MP_ROM_PTR(&krn_term_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_term_text),  MP_ROM_PTR(&krn_term_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_term_fill),  MP_ROM_PTR(&krn_term_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_kb_poll),    MP_ROM_PTR(&krn_kb_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_mouse),      MP_ROM_PTR(&krn_mouse_obj) },
    { MP_ROM_QSTR(MP_QSTR_screen_size),MP_ROM_PTR(&krn_screen_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_delay),      MP_ROM_PTR(&krn_delay_obj) },
};
STATIC MP_DEFINE_CONST_DICT(krn_module_globals, krn_module_globals_table);

const mp_obj_module_t krn_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&krn_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_krn, krn_module);
