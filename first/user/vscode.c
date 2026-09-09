// vscode.c - "开发"软件的 VSCode 模式
//
// 此模式直接承载 FSOS 原生 VSCode 风格代码编辑器 (editor.c): 切换为 VSCode
// 即进入原生编辑器 (多标签 / 侧边栏 / 语法高亮 / 命令面板 / 鼠标交互),
// 体验等同 VSCode, 全程运行于本操作系统内核, 无需依赖宿主。
//
// 按 D 切回内置 DevStudio (偏好持久化到 sysconf)。
#include "vscode.h"
#include "editor.h"
#include "devstudio.h"
#include "sysconf.h"

void vscode_open(void) {
    editor_open();               // 重新扫描磁盘文件列表并进入原生编辑器
}

int vscode_take_skip(void) {
    return editor_take_skip();   // 运行全屏程序后忽略一次鼠标边沿
}

int vscode_key(int k) {
    // 按 D/d 切回内置 DevStudio
    if (k == 68 || k == 100) {
        sysconf_set_dev_app(DEV_APP_DEVSTUDIO);
        devstudio_open();
        return 1;
    }
    // 其余按键交给原生编辑器处理
    return editor_key(k);
}

void vscode_draw(int x, int y, int w, int h) {
    editor_draw(x, y, w, h);
}
