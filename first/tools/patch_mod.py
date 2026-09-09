#!/usr/bin/env python3
# patch_mod.py - 为解释器模块 blob 回填头部字段
#
# 模块源码 (modules/*_mod.c) 编译链接后, 头部 mod_header_t 的 size / crc32 在编译期
# 未知, 留 0。本脚本在 objcopy 生成裸 blob 后:
#   * size  = 整个 blob 字节数 (含头部)
#   * crc32 = blob[32:size) 的 IEEE CRC32 (供内核加载器校验完整性)
#   * version = 1, entry_off = 32 (MOD_HDRSZ)
# 头部其余字段保持源码常量。
import sys, struct

def crc32(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF

def main():
    if len(sys.argv) < 2:
        print("usage: patch_mod.py <modfile> [...]")
        sys.exit(2)
    for p in sys.argv[1:]:
        with open(p, 'rb') as f:
            b = bytearray(f.read())
        if len(b) < 32:
            print('skip (too small):', p); continue
        if struct.unpack('<Q', b[0:8])[0] != 0x4F534D31:
            print('skip (bad magic):', p); continue
        size = len(b)
        crc = crc32(bytes(b[32:size]))
        # 头部布局: version@8 entry_off@12 size@16 crc32@20 flags@24 reserved@28
        struct.pack_into('<IIIII', b, 8, 1, 32, size, crc, 0)
        with open(p, 'wb') as f:
            f.write(b)
        print('patched %s  size=%d  crc=0x%08X' % (p, size, crc))

if __name__ == '__main__':
    main()
