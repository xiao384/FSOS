// jvm_mod.c - Java 26 SE 解释器模块 (Phase A: 管线验证骨架, Phase B 将填充完整解释器)
#include "mod_abi.h"

__attribute__((section(".modhdr"), used))
// version / entry_off 为常量; size / crc32 由构建脚本 (patch_mod.py) 在 objcopy 后回填,
// reserved 由静态初始化清零。入口固定在头部之后 (MOD_HDRSZ 偏移)。
static const mod_header_t g_mod_hdr = {
    MOD_MAGIC, MOD_ABI_VERSION, MOD_HDRSZ, 0, 0, 0, {0}
};

static void iprint(const mod_syscalls_t* sc, long v) {
    char buf[24]; int i = 0;
    if (v < 0) { sc->putc('-'); v = -v; }
    if (v == 0) buf[i++] = '0';
    else { char t[24]; int n = 0; while (v) { t[n++] = '0' + (v % 10); v /= 10; } while (n) buf[i++] = t[--n]; }
    buf[i] = 0;
    sc->puts(buf);
}

__attribute__((section(".module_entry")))
int module_entry(const mod_syscalls_t* sc, const char* src, const char* proc_name) {
    (void)proc_name;
    sc->puts("== FSOS Java 26 SE (jvm module) ==\n");
    int* t = (int*)sc->malloc(16);
    t[0] = 6; t[1] = 7;
    long r = (long)t[0] * t[1];
    sc->free(t);
    if (src) { sc->puts("src: "); sc->puts(src); sc->puts("\n"); }
    sc->puts("compute 6*7 = "); iprint(sc, r); sc->puts("\n");
    sc->puts("JVM_OK\n");
    return 0;
}
