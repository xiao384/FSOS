// installer.h - 图形化安装器入口
#ifndef INSTALLER_H
#define INSTALLER_H

// 若目标盘未安装标记, 进入图形安装向导并把系统写入硬盘, 然后重启 (不返回)。
// 声明为 weak: 第一遍链接(尚无内嵌镜像)时解析为 NULL, 不报错;
// 第二遍链接(加入 install_payload.obj + installer.o)时提供真实定义。
void installer_run(void) __attribute__((weak));

#endif // INSTALLER_H
