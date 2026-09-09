; install_payload.asm - 内嵌可安装磁盘镜像 (image.img)
; 构建系统在链接前生成 install_inc.inc, 内含 incbin 的绝对路径。
; 安装器运行时把这段镜像整体写入目标硬盘, 即完成系统安装。
section .rodata
global install_image
global install_image_size
install_image:
%include "install_inc.inc"
install_image_end:
install_image_size:
    dd install_image_end - install_image
