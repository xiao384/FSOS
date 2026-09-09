#!/usr/bin/env python3
"""通过 VNC (RFB 协议) 截取 VMware 虚拟机的屏幕画面。

用途
----
无头启动 (vmrun start <vmx> nogui) 时, `vmrun captureScreen` 需要 VMware Tools
(FSOS 没有), 无法验证 GUI 是否真的显示。VMware 支持 VMX 里的 `RemoteDisplay.vnc.*`
远端显示, 本工具实现一个最小 RFB 客户端, 取一帧并存成 PNG。

VMX 侧需开启 (make_uefi_vm.ps1 已默认写入, 且未设口令 => None 认证):
    RemoteDisplay.vnc.enabled = "TRUE"
    RemoteDisplay.vnc.port    = "5901"

用法
----
    python vnc_screenshot.py [--host 127.0.0.1] [--port 5901] [--out uefi_gui.png]

依赖: Pillow (存 PNG 用)。
"""

import argparse
import socket
import struct
import sys


class RFBError(Exception):
    pass


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise RFBError("connection closed (wanted %d bytes, got %d)" % (n, len(buf)))
        buf += chunk
    return buf


def recv_line(sock):
    """读到并含 '\\n' 为止 (RFB 握手阶段的 ASCII 行)。"""
    buf = b""
    while not buf.endswith(b"\n"):
        c = sock.recv(1)
        if not c:
            raise RFBError("connection closed during banner")
        buf += c
    return buf


def capture(host, port, timeout=10.0):
    """连上 VNC 取一帧, 返回 (PIL.Image, (width, height, desktop_name))。"""
    sock = socket.create_connection((host, port), timeout=timeout)
    try:
        banner = recv_line(sock).strip()
        if not banner.startswith(b"RFB "):
            raise RFBError("not an RFB server: %r" % banner)
        # 选服务端声明的版本 (VMware 一般 003.008)
        ver = banner[4:]
        choose = b"RFB 003.008\n" if ver >= b"003.008" else b"RFB 003.003\n"
        sock.sendall(choose)

        if choose == b"RFB 003.008\n":
            n = recv_exact(sock, 1)[0]
            types = list(recv_exact(sock, n))
        else:
            types = [struct.unpack(">I", recv_exact(sock, 4))[0]]

        if 1 not in types:  # 1 = None
            raise RFBError(
                "server requires auth (types=%s); 请在 VMX 中去掉 "
                "RemoteDisplay.vnc.password 以使用 None 认证" % types
            )
        sock.sendall(bytes([1]))

        if struct.unpack(">I", recv_exact(sock, 4))[0] != 0:
            raise RFBError("security handshake failed")

        sock.sendall(bytes([1]))  # ClientInit: shared
        width, height = struct.unpack(">HH", recv_exact(sock, 4))
        recv_exact(sock, 16)  # 服务端像素格式
        name_len = struct.unpack(">I", recv_exact(sock, 4))[0]
        name = recv_exact(sock, name_len).decode("utf-8", "replace")

        # 请求 32bpp 小端真彩色 (XRGB), 便于直接交给 Pillow
        # 16 字节: bpp(1) depth(1) bigendian(1) truecolor(1) rmax(2) gmax(2) bmax(2)
        #          rshift(1) gshift(1) bshift(1) padding(3)
        pf = struct.pack(">BBBBHHHBBB",
                         32, 24, 0, 1,           # bpp, depth, little-endian, truecolor
                         255, 255, 255,          # r/g/b max
                         16, 8, 0) + b"\x00" * 3  # r/g/b shift + padding
        sock.sendall(bytes([0]) + b"\x00" * 3 + pf)   # SetPixelFormat
        sock.sendall(struct.pack(">BBHi", 2, 0, 1, 0))  # SetEncodings: Raw
        sock.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, width, height))

        msg_type = recv_exact(sock, 1)[0]
        if msg_type != 0:
            raise RFBError("unexpected server message %d (want FramebufferUpdate)" % msg_type)
        recv_exact(sock, 1)  # padding
        nrect = struct.unpack(">H", recv_exact(sock, 2))[0]

        from PIL import Image
        img = Image.new("RGB", (width, height))
        for _ in range(nrect):
            rx, ry, rw, rh = struct.unpack(">HHHH", recv_exact(sock, 8))
            enc = struct.unpack(">i", recv_exact(sock, 4))[0]
            if enc != 0:
                raise RFBError("unsupported encoding %d (only Raw)" % enc)
            data = bytearray(recv_exact(sock, rw * rh * 4))
            # 我们请求的像素格式是 little-endian R<<16|G<<8|B (rshift=16,8,0):
            # 线上字节序为 [B,G,R,X], 而 PIL "RGBX" 期望 [R,G,B,X], 需每像素交换 byte0/byte2.
            for i in range(0, len(data), 4):
                data[i], data[i + 2] = data[i + 2], data[i]
            img.paste(Image.frombytes("RGBX", (rw, rh), bytes(data)).convert("RGB"), (rx, ry))
        return img, (width, height, name)
    finally:
        sock.close()


def main():
    ap = argparse.ArgumentParser(description="通过 VNC 截取 VMware 虚拟机屏幕")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5901)
    ap.add_argument("--out", default="uefi_gui.png")
    args = ap.parse_args()

    try:
        img, (w, h, name) = capture(args.host, args.port)
    except Exception as e:
        print("capture failed: %s" % e, file=sys.stderr)
        return 1
    img.save(args.out)
    print("saved %s (%dx%d, desktop '%s')" % (args.out, w, h, name))
    colors = img.getcolors(maxcolors=1 << 24)
    print("distinct colors: %d" % (len(colors) if colors else ">16M"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
