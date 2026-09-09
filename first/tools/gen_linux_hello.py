#!/usr/bin/env python3
# gen_linux_hello.py - 生成最小 Linux x86-64 PIE ELF 测试程序
#
# 该 ELF 不依赖任何 libc, 仅用裸 syscall 做:
#     write(1, "Hello from Linux!\n", 18)
#     exit(0)
# 位置无关 (rip 相对取串), 无重定位 / 无解释器, 可直接被 FSOS 的 Linuxulator
# (linux_exec) 在 LINUX_BASE 处加载运行。
#
# 用法:  python first/tools/gen_linux_hello.py [输出名]
# 默认输出: HELLO.ELF (与 wm.c 中 Ctrl+Alt+L 热键查找的名字一致)
import sys, struct

OUT = sys.argv[1] if len(sys.argv) > 1 else "HELLO.ELF"

# ---- 机器码 (位置无关) ----
# 0:  mov edx, 18            ; len
# 5:  lea rsi, [rip+0x15]    ; buf (msg @ offset 33, rip=12 => 12+0x15=33)
# 12: mov edi, 1             ; fd
# 17: mov eax, 1             ; sys_write
# 22: syscall
# 24: xor edi, edi           ; status 0
# 26: mov eax, 60            ; sys_exit
# 31: syscall
code = bytes([
    0xBA, 0x12, 0x00, 0x00, 0x00,
    0x48, 0x8D, 0x35, 0x15, 0x00, 0x00, 0x00,
    0xBF, 0x01, 0x00, 0x00, 0x00,
    0xB8, 0x01, 0x00, 0x00, 0x00,
    0x0F, 0x05,
    0x31, 0xFF,
    0xB8, 0x3C, 0x00, 0x00, 0x00,
    0x0F, 0x05,
])
msg = b"Hello from Linux!\n"          # 18 字节
assert len(code) == 33, len(code)
assert len(msg) == 18, len(msg)

EHDR_SIZE = 64
PHDR_SIZE = 56
CODE_OFF  = EHDR_SIZE + PHDR_SIZE     # 120
filesz = len(code) + len(msg)         # 50

# ---- ELF 头 ----
e_ident = (b"\x7fELF" + bytes([2,1,1,0,0]) + b"\x00"*7)
ehdr = e_ident + struct.pack("<HHIQQQIHHHHHH",
    3,            # e_type = ET_DYN (PIE)
    0x3E,         # e_machine = x86-64
    1,            # e_version
    0,            # e_entry (PIE: 相对偏移 0)
    EHDR_SIZE,    # e_phoff
    0,            # e_shoff
    0,            # e_flags
    EHDR_SIZE,    # e_ehsize
    PHDR_SIZE,    # e_phentsize
    1,            # e_phnum
    0, 0, 0,      # e_shentsize, e_shnum, e_shstrndx
)

# ---- 程序头 (PT_LOAD, RX) ----
phdr = struct.pack("<IIQQQQQQ",
    1,            # p_type = PT_LOAD
    5,            # p_flags = PF_X|PF_R
    CODE_OFF,     # p_offset
    0,            # p_vaddr (相对基址)
    0,            # p_paddr
    filesz,       # p_filesz
    filesz,       # p_memsz
    0x1000,       # p_align
)

blob = bytearray(ehdr + phdr)
blob += code
blob += msg
# 补齐到 4KB 扇区对齐, 方便写入磁盘镜像
while len(blob) % 512:
    blob.append(0)

with open(OUT, "wb") as f:
    f.write(blob)

# ---- 自检: 解析回来确认合法 ----
assert blob[0:4] == b"\x7fELF"
e = struct.unpack_from("<HHIQQQIHHHHHH", blob, 16)
assert e[0] == 3 and e[1] == 0x3E, e
print(f"wrote {OUT}: {len(blob)} bytes (PIE ET_DYN, {filesz}B loadable, entry=base+0)")
