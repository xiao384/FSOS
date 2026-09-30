#!/usr/bin/env python3
"""Headless FSOS GUI screenshot harness.

Boots output/image.img in QEMU (VNC + serial), auto-logs-in as admin via QMP
send-key, then grabs the desktop framebuffer via VNC and saves a PNG. Useful for
visual regression checks of the wallpaper / topbar / dock / control-center.

Usage:
    python tools/fsos_gui_shot.py [--out desktop.png] [--seconds 18]
"""
import argparse
import os
import socket
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
IMG = os.path.join(REPO, "output", "image.img")
SER = os.path.join(REPO, "output", "boot_serial.log")
QMP_PORT = 4444
VNC_PORT = 5901


def find_qemu():
    import host_platform  # noqa
    facts = host_platform.detect()
    q = facts.tools.get("qemu") or facts.resolve_tool(
        ["qemu-system-x86_64", "qemu-system-x86_64.exe"])
    return q or "qemu-system-x86_64"


# ---- QMP --------------------------------------------------------------------
def qmp_connect(port):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    greet = s.recv(4096)
    if b"QMP" not in greet:
        raise RuntimeError("QMP handshake failed: %r" % greet[:80])
    s.sendall(b'{"execute":"qmp_capabilities"}\n')
    s.recv(4096)
    return s


def qmp_send(s, cmd):
    s.sendall((cmd + "\n").encode())
    time.sleep(0.15)
    try:
        return s.recv(65536)
    except Exception:
        return b""


def qmp_send_key(s, qcodes):
    keys = ",".join('{"type":"qcode","data":"%s"}' % k for k in qcodes)
    qmp_send(s, '{"execute":"send-key","arguments":{"keys":[%s]}}' % keys)


def _qmp_move(s, x, y):
    """Drive the relative PS/2 pointer to (x,y) by injecting motion deltas."""
    steps = 24
    for i in range(1, steps + 1):
        nx = int(x * i / steps)
        ny = int(y * i / steps)
        dx = nx - int(x * (i - 1) / steps)
        dy = ny - int(y * (i - 1) / steps)
        ev = ('{"execute":"input-send-event","arguments":{"events":['
              '{"type":"rel","data":{"axis":"x","value":%d}},'
              '{"type":"rel","data":{"axis":"y","value":%d}}]}}' % (dx, dy))
        qmp_send(s, ev)
        time.sleep(0.03)


# ---- VNC framebuffer capture (minimal RFB) ----------------------------------
def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise RuntimeError("VNC closed (wanted %d, got %d)" % (n, len(buf)))
        buf += chunk
    return buf


def vnc_capture(port, out, timeout=10.0):
    from PIL import Image
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    recv_exact(s, 12)   # server version banner ("RFB 003.008\n")
    s.sendall(b"RFB 003.008\n")
    n = recv_exact(s, 1)[0]
    recv_exact(s, n)
    s.sendall(b"\x01")  # None auth
    if struct.unpack(">I", recv_exact(s, 4))[0] != 0:
        raise RuntimeError("VNC auth failed")
    s.sendall(b"\x01")  # shared
    w, h = struct.unpack(">HH", recv_exact(s, 4))
    recv_exact(s, 16)
    nlen = struct.unpack(">I", recv_exact(s, 4))[0]
    recv_exact(s, nlen)
    pf = struct.pack(">BBBBHHHBBB", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0) + b"\x00" * 3
    s.sendall(b"\x00" + b"\x00" * 3 + pf)
    s.sendall(struct.pack(">BBHi", 2, 0, 1, 0))  # SetEncodings: Raw
    s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))  # FramebufferUpdateRequest
    mt = recv_exact(s, 1)[0]
    recv_exact(s, 1)
    nrect = struct.unpack(">H", recv_exact(s, 2))[0]
    img = Image.new("RGB", (w, h))
    for _ in range(nrect):
        rx, ry, rw, rh = struct.unpack(">HHHH", recv_exact(s, 8))
        enc = struct.unpack(">i", recv_exact(s, 4))[0]
        if enc != 0:
            raise RuntimeError("unsupported VNC encoding %d" % enc)
        data = bytearray(recv_exact(s, rw * rh * 4))
        for i in range(0, len(data), 4):
            data[i], data[i + 2] = data[i + 2], data[i]
        img.paste(Image.frombytes("RGBX", (rw, rh), bytes(data)).convert("RGB"), (rx, ry))
    s.close()
    img.save(out)
    print("[shot] saved %s (%dx%d)" % (out, w, h))
    return w, h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "output", "desktop.png"))
    ap.add_argument("--seconds", type=int, default=20)
    args = ap.parse_args()

    if not os.path.isfile(IMG):
        print("[shot] missing %s (build first)" % IMG, file=sys.stderr)
        return 2
    qemu = find_qemu()
    if not qemu:
        print("[shot] QEMU not found", file=sys.stderr)
        return 4

    # 清理可能残留的 QEMU (避免占用 VNC/QMP 端口)
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/F", "/IM", "qemu-system-x86_64.exe"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            subprocess.run(["pkill", "-f", "qemu-system-x86_64"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass
    time.sleep(1.0)

    cmd = [
        qemu, "-drive", "file=%s,format=raw" % IMG.replace("\\", "/"),
        "-m", "256", "-no-reboot", "-display", "none",
        "-vnc", ":%d" % (VNC_PORT - 5900),
        "-qmp", "tcp:127.0.0.1:%d,server,nowait" % QMP_PORT,
        "-serial", "file:%s" % SER.replace("\\", "/"),
    ]
    print("[shot] launching QEMU")
    proc = subprocess.Popen(cmd)
    try:
        # wait for boot to reach login (kernel prints "load users")
        for _ in range(60):
            try:
                txt = open(SER, "rb").read().decode("utf-8", "replace")
            except OSError:
                txt = ""
            if "load users" in txt:
                break
            if proc.poll() is not None:
                print("[shot] QEMU exited early (code %s)" % proc.returncode)
                return 1
            time.sleep(0.5)
        else:
            print("[shot] timeout waiting for login; capturing anyway")
        time.sleep(1.0)

        s = qmp_connect(QMP_PORT)
        # login: Enter (select admin) -> type 'admin' -> Enter
        qmp_send_key(s, ["ret"])
        time.sleep(0.4)
        for ch in "admin":
            qmp_send_key(s, [ch])
            time.sleep(0.25)
        qmp_send_key(s, ["ret"])
        s.close()
        print("[shot] logged in, waiting for desktop")
        time.sleep(4.0)

        # best-effort: open control center by clicking the top-right cluster
        try:
            from vnc_screenshot import capture as _c
        except Exception:
            _c = None
        # capture desktop
        w, h = vnc_capture(VNC_PORT, args.out, timeout=12.0)
        print("[shot] desktop %dx%d captured; also try control-center shot" % (w, h))

        # second shot: move pointer to top-right cluster via QMP and click to
        # toggle the control center, then capture again.
        try:
            cx, cy = max(10, w - 24), 18
            qs = qmp_connect(QMP_PORT)
            _qmp_move(qs, cx, cy)
            qmp_send(qs, '{"execute":"input-send-event","arguments":{"events":['
                          '{"type":"btn","data":{"down":true,"button":"left"}}]}}')
            time.sleep(0.2)
            qmp_send(qs, '{"execute":"input-send-event","arguments":{"events":['
                          '{"type":"btn","data":{"down":false,"button":"left"}}]}}')
            qs.close()
            time.sleep(1.2)
            vnc_capture(VNC_PORT, os.path.join(REPO, "output", "control_center.png"),
                        timeout=12.0)
            print("[shot] control-center shot at (%d,%d)" % (cx, cy))
        except Exception as e:
            print("[shot] control-center capture skipped: %s" % e)
    finally:
        try:
            proc.terminate()
        except Exception:
            pass
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    return 0


def _capture_click(port, out, cx, cy, timeout=12.0):
    """Capture after sending a VNC pointer click at (cx,cy)."""
    from PIL import Image
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    recv_exact(s, 12)   # server version banner
    s.sendall(b"RFB 003.008\n")
    n = recv_exact(s, 1)[0]
    recv_exact(s, n)
    s.sendall(b"\x01")
    if struct.unpack(">I", recv_exact(s, 4))[0] != 0:
        raise RuntimeError("VNC auth failed")
    s.sendall(b"\x01")
    w, h = struct.unpack(">HH", recv_exact(s, 4))
    recv_exact(s, 16)
    nlen = struct.unpack(">I", recv_exact(s, 4))[0]
    recv_exact(s, nlen)
    # PS/2 mouse is relative: sweep the pointer in small steps so QEMU emits
    # movement deltas the guest driver can follow, then click at (cx,cy).
    steps = 48
    for i in range(1, steps + 1):
        px = int(cx * i / steps)
        py = int(cy * i / steps)
        s.sendall(struct.pack(">BBHH", 5, 0, px, py))
        time.sleep(0.02)
    time.sleep(0.25)
    s.sendall(struct.pack(">BBHH", 5, 1, cx, cy))  # button down
    time.sleep(0.15)
    s.sendall(struct.pack(">BBHH", 5, 0, cx, cy))  # button up
    time.sleep(0.5)
    pf = struct.pack(">BBBBHHHBBB", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0) + b"\x00" * 3
    s.sendall(b"\x00" + b"\x00" * 3 + pf)
    s.sendall(struct.pack(">BBHi", 2, 0, 1, 0))
    s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))
    recv_exact(s, 1); recv_exact(s, 1)
    nrect = struct.unpack(">H", recv_exact(s, 2))[0]
    img = Image.new("RGB", (w, h))
    for _ in range(nrect):
        rx, ry, rw, rh = struct.unpack(">HHHH", recv_exact(s, 8))
        enc = struct.unpack(">i", recv_exact(s, 4))[0]
        if enc != 0:
            raise RuntimeError("enc %d" % enc)
        data = bytearray(recv_exact(s, rw * rh * 4))
        for i in range(0, len(data), 4):
            data[i], data[i + 2] = data[i + 2], data[i]
        img.paste(Image.frombytes("RGBX", (rw, rh), bytes(data)).convert("RGB"), (rx, ry))
    s.close()
    img.save(out)
    print("[shot] saved (click) %s" % out)


if __name__ == "__main__":
    sys.exit(main())
