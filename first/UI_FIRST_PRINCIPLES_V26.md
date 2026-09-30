# FSOS v26.1 第一性原理 UI / 电源修复\n\n本版围绕用户实际 VMware 截图重新检查了 UI 的基础闭环：\n\n1. 桌面背景必须覆盖完整 framebuffer，不能给 Dock 单独留下旧的黑色时代界面区。\n2. Dock 必须由同一套几何参数负责绘制与命中测试，避免“看得见但点不到”。\n3. 所有桌面图标改为统一的矢量几何语言，减少旧版方块图标的割裂感。\n4. Terminal client 使用现代 GUI 字体与动态行列，避免 8x8 legacy 文本在高分辨率窗口中缩成小块。\n5. UEFI 把 ACPI RSDP 从 ConfigurationTable 复制到 0x6500，内核优先使用它发现 FADT/DSDT。\n6. 关机逻辑不再以 `cli; hlt` 作为成功路径；`cli; hlt` 只会停止当前 CPU，不能代表 ACPI system power-off。\n

## v26.1 截图问题修复
- 直接 LFB/VBE 路径标记为 native，避免后续旧版 GOP 320x200 镜像器覆盖真正的高分辨率桌面。
- legacy GOP 镜像路径也改为铺满整个画布，彻底取消 1920x1080 上的固定黑边。
- 顶栏右侧改用从右到左布局，时间、电池、声音、Wi-Fi、控制中心不再互相覆盖。
- Dock 图标与 tile 尺寸收紧，避免图标重叠和过大的白色块。
- 开始菜单注销/关机改为精确命中。
- MicroPython 的 krn.poweroff / krn.reboot 与桌面统一走电源服务，不再用 `cli; hlt`。
