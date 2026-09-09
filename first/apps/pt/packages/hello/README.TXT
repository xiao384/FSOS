hello 示例包
============

这是随 Better terminal (pt 移植版) 一起内嵌进内核的演示包, 用来验证
"打包 -> 解压 -> 安装 -> 运行"这条链路。

用法 (在 FSOS 内核终端里):

    bt                 进入 Better terminal
    pkg list           列出可用包 (内嵌包标记为 [内嵌])
    install hello      解包到文件区并登记
    ls                 查看解出来的文件
    run hello          执行入口 main.py
    pkg remove hello   卸载

包本身就是一个普通 .zip: 用系统自带的 zip 工具把任意目录打包,
放到 apps/pt/packages/<包名>/ 下, 再运行

    python tools/bundle_pt.py --frozen

就会重新生成内嵌包并刷新内核里的终端源码。
