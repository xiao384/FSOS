# 通用引导验证: 检查 RIP 在内核范围 + 内核到达 GUI 时输出的 COM1 哨兵。
# 说明: 早期曾用固定物理地址 0x7A00 的魔数判定, 但该地址可能被内核运行期清零
# (虚拟 0x7A00 经恒等页表映射物理 0x7A00, 但某运行期行为会偶发清零), 不可靠。
# 改用 COM1 哨兵字符串 "FSOS_BOOT_OK": 内核 kernel_main 到达 GUI 后打印,
# bootcheck 抓取 QEMU 的 -serial 文件判定。辅以 gdb 读 RIP 确认已进长模式内核。
# 用法: python bootcheck.py <镜像路径> [--iso] [--port N]
import subprocess, socket, time, re, sys, os, tempfile

QEMU = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
BOOT_SENTINEL = b"FSOS_BOOT_OK"
BOOT_MAGIC = 0x424F4F54   # 仍写 0x7A00, 仅作弱信号

def read_rip(port):
    gs = r"e:\project\clion\project_system\first\bootcheck.gdb"
    with open(gs, "w") as f:
        f.write(f"""target remote :{port}
set pagination off
printf "RIP=0x%llx\\n", $rip
detach
quit
""")
    r = subprocess.run(["gdb", "-nx", "-batch", "-x", gs],
                       capture_output=True, text=True, timeout=30)
    m = re.search(r"RIP=0x([0-9a-fA-F]+)", r.stdout)
    return int(m.group(1), 16) if m else 0

def main():
    img = sys.argv[1]
    as_iso = "--iso" in sys.argv
    port = 12390 + int(sys.argv[sys.argv.index("--port")+1]) if "--port" in sys.argv else 12390
    if not os.path.exists(img):
        print("missing:", img); return 1

    serf = tempfile.mktemp(prefix="ser_", suffix=".log")
    cmd = [QEMU, "-drive", f"file={img},format={'vmdk' if img.endswith('.vmdk') else 'raw'}",
           "-serial", f"file:{serf}"]
    if as_iso:
        cmd = [QEMU, "-cdrom", img, "-boot", "d", "-serial", f"file:{serf}"]
    cmd += ["-m", "256", "-display", "none", "-gdb", f"tcp::{port}", "-no-reboot"]
    q = subprocess.Popen(cmd)
    time.sleep(12)   # 等待内核走完初始化到达 GUI 并打印哨兵

    # 先读哨兵再连 gdb: gdb target remote 会暂停 CPU, 避免影响尚未打印的哨兵
    sentinel = b""
    try:
        with open(serf, "rb") as f:
            sentinel = f.read()
    except Exception:
        pass
    rip = read_rip(port)
    q.terminate()
    time.sleep(0.3)
    try: os.remove(serf)
    except Exception: pass

    reached_gui = BOOT_SENTINEL in sentinel
    in_kernel   = 0x100000 <= rip < 0x200000
    ok = reached_gui and in_kernel
    print(f"{os.path.basename(img)}: RIP=0x{rip:X} gui_sentinel={reached_gui} -> {'OK' if ok else 'FAIL'}")
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
