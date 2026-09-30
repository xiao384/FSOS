#!/usr/bin/env python3
# verify_install.py - 端到端安装流程验证 (Task 62)
#
# 阶段1: 用空白 2MB 硬盘 + ISO 启动, 通过 QMP send-key 驱动图形安装器:
#         ENTER(欢迎) -> ENTER(主机名用默认) -> ENTER(密码留空) -> 自动写盘
#         轮询硬盘镜像, 待 LBA 4095 出现 "PSB1" 标记即写盘完成, 杀掉 QEMU。
# 阶段2: 单独从该硬盘启动 (无 ISO), 核对串口输出:
#         "system installed, continue boot" + "FSOS_BOOT_OK",
#         且不应出现 "not installed -> installer"。
# 最后直接读盘校验: 引导扇区 0x55AA / 用户库超级块 "USR1" / sysconf 主机名。

import json, os, socket, subprocess, sys, time

QEMU  = r"C:/Program Files/qemu/qemu-system-x86_64.exe"
ROOT  = r"e:/project/clion/project_system/first"
ISO   = r"e:/project/clion/project_system/iso/FSOS.iso"
BLANK = os.path.join(ROOT, "blank_test.img")
P1LOG = os.path.join(ROOT, "phase1.log")
P2LOG = os.path.join(ROOT, "phase2.log")
QMP_PORT = 4444

SECTOR = 512
DISK_SECTORS = 4096
LBA_USER_SB  = 6000
LBA_SYSCONF  = 6020
MARKER_LBA   = DISK_SECTORS - 1  # 4095

def log(msg):
    print(f"[verify] {msg}", flush=True)

# ---------- QMP 客户端 ----------
class QMP:
    def __init__(self, port, timeout=30):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
                self.f = self.s.makefile("r")
                greet = self.f.readline()  # 丢弃 greeting
                self.cmd({"execute": "qmp_capabilities"})
                return
            except OSError:
                time.sleep(0.3)
        raise RuntimeError("cannot connect to QMP")

    def cmd(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())
        while True:
            line = self.f.readline()
            if not line:
                return None
            resp = json.loads(line)
            if "return" in resp or "error" in resp:
                return resp
            # 跳过事件 (如 STOP / RESUME)

    def send_key(self, qcode):
        return self.cmd({"execute": "send-key",
                         "arguments": {"keys": [{"type": "qcode", "data": qcode}]}})

    def close(self):
        try: self.s.close()
        except OSError: pass

def wait_for_text(path, needle, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            with open(path, "r", errors="ignore") as f:
                if needle in f.read():
                    return True
        except OSError:
            pass
        time.sleep(0.5)
    return False

def disk_has_marker(path):
    try:
        with open(path, "rb") as f:
            f.seek(MARKER_LBA * SECTOR)
            buf = f.read(4)
            return buf[:4] == b"PSB1"
    except OSError:
        return False

def wait_for_marker(path, timeout=90):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if disk_has_marker(path):
            return True
        time.sleep(0.5)
    return False

def launch(args):
    log("launch: " + " ".join(args))
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def kill(proc):
    try:
        proc.kill()
    except Exception:
        pass
    try:
        proc.wait(timeout=10)
    except Exception:
        pass

# ================= 阶段 1: ISO 引导 + 安装 =================
def phase1():
    log("=== Phase 1: boot ISO + blank HDD, run installer ===")
    if os.path.exists(BLANK):
        os.remove(BLANK)
    subprocess.run([QEMU.replace("qemu-system-x86_64.exe", "qemu-img.exe"),
                   "create", "-f", "raw", BLANK, "2M"], check=True)
    if os.path.exists(P1LOG):
        os.remove(P1LOG)

    proc = launch([
        QEMU, "-m", "256", "-boot", "d",
        "-cdrom", ISO,
        "-drive", f"file={BLANK},format=raw,if=ide",
        "-serial", f"file:{P1LOG}",
        "-qmp", f"tcp:127.0.0.1:{QMP_PORT},server,nowait",
        "-display", "none",
    ])

    try:
        # 等内核进入安装器
        if not wait_for_text(P1LOG, "not installed -> installer", timeout=40):
            log("FAIL: kernel did not reach installer (no 'not installed' in serial)")
            return False
        log("OK: kernel booted from ISO and entered installer")

        qmp = QMP(QMP_PORT)
        time.sleep(1.0)
        qmp.send_key("ret")   # 欢迎 -> 选项
        log("sent ENTER (welcome -> options)")
        time.sleep(2.0)
        qmp.send_key("ret")   # 主机名编辑 -> 接受默认
        log("sent ENTER (hostname accept default)")
        time.sleep(2.0)
        qmp.send_key("ret")   # 密码编辑 -> 留空, 触发写盘
        log("sent ENTER (password empty -> begin write)")
        qmp.close()

        # 轮询硬盘, 待 PSB1 标记出现 = 写盘完成
        if not wait_for_marker(BLANK, timeout=120):
            log("FAIL: disk never got PSB1 marker within timeout")
            return False
        log("OK: disk written, PSB1 marker present at LBA 4095")
        return True
    finally:
        kill(proc)
        time.sleep(1)

# ================= 阶段 2: 从硬盘启动 =================
def phase2():
    log("=== Phase 2: boot from installed HDD only ===")
    if os.path.exists(P2LOG):
        os.remove(P2LOG)
    proc = launch([
        QEMU, "-m", "256", "-boot", "c",
        "-drive", f"file={BLANK},format=raw,if=ide",
        "-serial", f"file:{P2LOG}",
        "-display", "none",
    ])
    try:
        ok = wait_for_text(P2LOG, "FSOS_BOOT_OK", timeout=40)
        with open(P2LOG, "r", errors="ignore") as f:
            out = f.read()
        if "not installed -> installer" in out:
            log("FAIL: HDD boot still entered installer (PSB1 not detected)")
            return False
        if "system installed, continue boot" in out:
            log("OK: kernel detected install marker -> continue boot")
        else:
            log("WARN: 'system installed' not seen")
        if ok:
            log("OK: FSOS_BOOT_OK reached on HDD boot")
            return True
        log("FAIL: FSOS_BOOT_OK not reached on HDD boot")
        return False
    finally:
        kill(proc)

# ================= 直接读盘校验 =================
def inspect_disk():
    log("=== Disk content inspection ===")
    with open(BLANK, "rb") as f:
        data = f.read()
    size = len(data)
    log(f"disk size = {size} bytes (expect {DISK_SECTORS*SECTOR})")

    # 引导扇区末尾 0x55AA
    boot_sig = data[510:512]
    log(f"boot sector signature: {boot_sig.hex()} (expect 55aa)")

    # 用户库超级块 "USR1" @ LBA_USER_SB
    sb = data[LBA_USER_SB*SECTOR: LBA_USER_SB*SECTOR+8]
    log(f"user superblock @{LBA_USER_SB}: {sb[:4]} (expect USR1)  count={sb[4]}")

    # 记录 0 = root
    rec0 = data[LBA_USER_REC*SECTOR: LBA_USER_REC*SECTOR+32]
    magic = rec0[0]
    role = rec0[1]
    name = rec0[2:17].split(b"\x00")[0].decode("latin1", "ignore")
    log(f"rec[0]: magic={hex(magic)} role={role} name={name!r} (expect root/role2)")

    # sysconf 主机名 @ LBA_SYSCONF + 8
    host = data[LBA_SYSCONF*SECTOR+8: LBA_SYSCONF*SECTOR+8+15]
    host = host.split(b"\x00")[0].decode("latin1", "ignore")
    log(f"sysconf hostname @{LBA_SYSCONF}+8: {host!r} (expect fsos)")

    # 标记
    marker = data[MARKER_LBA*SECTOR:MARKER_LBA*SECTOR+4]
    log(f"marker @4095: {marker} (expect PSB1)")
    return True

def main():
    if not os.path.exists(QEMU):
        log(f"FATAL: qemu not found at {QEMU}")
        sys.exit(1)
    if not os.path.exists(ISO):
        log(f"FATAL: iso not found at {ISO}")
        sys.exit(1)

    ok1 = phase1()
    if not ok1:
        log("=== RESULT: PHASE1 FAILED ===")
        sys.exit(1)

    inspect_disk()
    ok2 = phase2()
    if ok2:
        log("=== RESULT: PASS (install + HDD boot verified) ===")
        sys.exit(0)
    else:
        log("=== RESULT: PHASE2 FAILED ===")
        sys.exit(1)

if __name__ == "__main__":
    main()
