// power.c - minimal ACPI power-control implementation for FSOS
//
// First principles: halting the current CPU is an idle/dead-stop primitive, not a system
// power transition. Real shutdown on x86 is requested through the
// ACPI FADT + DSDT `_S5_` power state. This module discovers those structures at
// runtime, writes PM1a/PM1b control, and only uses the known QEMU ports as a fallback.

#include <stdint.h>
#include "io.h"

#define ACPI_RSDP_SIG "RSD PTR "
#define ACPI_FADT_SIG 0x50434146UL /* "FACP" little-endian */
#define ACPI_RSDT_SIG 0x54445352UL /* "RSDT" */
#define ACPI_XSDT_SIG 0x54445358UL /* "XSDT" */
#define ACPI_SDT_MIN  36
#define ACPI_RSDP_ADDR 0x6500UL  /* UEFI copies the firmware RSDP here */

typedef struct {
    const uint8_t *p;
    uint32_t len;
    uint32_t revision;
} acpi_rsdp_t;

typedef struct {
    uint32_t addr;
    uint32_t len;
    uint32_t pm1a_cnt;
    uint32_t pm1b_cnt;
    uint8_t  pm1_cnt_len;
    uint32_t dsdt;
    uint64_t x_dsdt;
    uint8_t  reset_reg_space;
    uint8_t  reset_reg_bitwidth;
    uint16_t reset_reg_port;
    uint8_t  reset_value;
} acpi_fadt_t;

static uint16_t rd16(const void *vp) { const uint8_t *p=(const uint8_t*)vp; return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static uint32_t rd32(const void *vp) { const uint8_t *p=(const uint8_t*)vp; return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t rd64(const void *vp) { return (uint64_t)rd32(vp)|((uint64_t)rd32((const uint8_t*)vp+4)<<32); }
static int sig8(const uint8_t *p, const char *s) { for(int i=0;i<8;i++) if(p[i]!=(uint8_t)s[i]) return 0; return 1; }
static int checksum8(const uint8_t *p, uint32_t n) { uint8_t s=0; for(uint32_t i=0;i<n;i++) s=(uint8_t)(s+p[i]); return s==0; }
static int sdt_ok(const uint8_t *p, uint32_t min_len) {
    if (!p) return 0;
    uint32_t n=rd32(p+4);
    if (n<min_len || n>0x01000000UL) return 0;
    return checksum8(p,n);
}

static const uint8_t *find_rsdp(void) {
    // UEFI path: main.c copies the authoritative firmware RSDP into low memory.
    // Prefer it so VMware/UEFI does not depend on legacy BIOS memory scans.
    const uint8_t *copy=(const uint8_t*)(uintptr_t)ACPI_RSDP_ADDR;
    if(sig8(copy,ACPI_RSDP_SIG) && checksum8(copy,20)) {
        if(copy[15]>=2) {
            uint32_t n=rd32(copy+20);
            if(n>=36 && checksum8(copy,n)) return copy;
        } else return copy;
    }
    // BIOS fallback: EBDA first (1 KiB), then BIOS area. ACPI requires 16-byte alignment.
    uint16_t ebda_seg=*(volatile uint16_t*)(uintptr_t)0x40E;
    uintptr_t ebda=(uintptr_t)ebda_seg<<4;
    if (ebda>=0x80000UL && ebda<0xA0000UL) {
        for(uintptr_t p=ebda;p<ebda+1024;p+=16) {
            const uint8_t *r=(const uint8_t*)p;
            if(sig8(r,ACPI_RSDP_SIG) && checksum8(r,20)) {
                if(r[15]>=2) { uint32_t n=rd32(r+20); if(n>=36 && checksum8(r,n)) return r; }
                else return r;
            }
        }
    }
    for(uintptr_t p=0xE0000UL;p<0x100000UL;p+=16) {
        const uint8_t *r=(const uint8_t*)p;
        if(sig8(r,ACPI_RSDP_SIG) && checksum8(r,20)) {
            if(r[15]>=2) { uint32_t n=rd32(r+20); if(n>=36 && checksum8(r,n)) return r; }
            else return r;
        }
    }
    return 0;
}

static const uint8_t *find_table(uint32_t wanted) {
    const uint8_t *r=find_rsdp();
    if(!r) return 0;
    uint8_t rev=r[15];
    uintptr_t root=0; uint32_t entries=0, stride=0;
    if(rev>=2 && rd64(r+24)!=0) { root=(uintptr_t)rd64(r+24); stride=8; }
    else { root=(uintptr_t)rd32(r+16); stride=4; }
    if(!root) return 0;
    const uint8_t *h=(const uint8_t*)root;
    if(!sdt_ok(h,ACPI_SDT_MIN)) return 0;
    uint32_t total=rd32(h+4);
    if(total<ACPI_SDT_MIN) return 0;
    entries=(total-ACPI_SDT_MIN)/stride;
    for(uint32_t i=0;i<entries;i++) {
        uint64_t a=(stride==8)?rd64(h+ACPI_SDT_MIN+i*8):rd32(h+ACPI_SDT_MIN+i*4);
        if(!a || a>0xFFFFFFFFULL) continue;
        const uint8_t *t=(const uint8_t*)(uintptr_t)a;
        if(rd32(t)==wanted && sdt_ok(t,ACPI_SDT_MIN)) return t;
    }
    return 0;
}

static int get_fadt(acpi_fadt_t *out) {
    const uint8_t *f=find_table(ACPI_FADT_SIG);
    if(!f) return -1;
    uint32_t n=rd32(f+4);
    if(n<100) return -1;
    out->addr=(uint32_t)(uintptr_t)f; out->len=n;
    out->pm1a_cnt=rd32(f+64); out->pm1b_cnt=rd32(f+68); out->pm1_cnt_len=f[89];
    out->dsdt=rd32(f+40); out->x_dsdt=(n>=148)?rd64(f+140):(uint64_t)out->dsdt;
    out->reset_reg_space=0; out->reset_reg_bitwidth=0; out->reset_reg_port=0; out->reset_value=0;
    if(n>=132) { // GenericAddressStructure at FADT+116, reset value at +128
        out->reset_reg_space=f[116];
        out->reset_reg_bitwidth=f[117];
        out->reset_reg_port=rd16(f+120);
        out->reset_value=f[128];
    }
    return 0;
}

static int aml_int(const uint8_t *p, uint32_t remain, uint32_t *used) {
    if(!p || !remain) return -1;
    switch(p[0]) {
        case 0x00: *used=1; return 0;
        case 0x01: *used=1; return 1;
        case 0x0A: if(remain<2) return -1; *used=2; return p[1];
        case 0x0B: if(remain<3) return -1; *used=3; return rd16(p+1);
        case 0x0C: if(remain<5) return -1; *used=5; return (int)rd32(p+1);
        case 0x0E: if(remain<9) return -1; *used=9; return (int)rd64(p+1);
        default: return -1;
    }
}

static int find_s5(uint16_t *a, uint16_t *b) {
    acpi_fadt_t f; if(get_fadt(&f)!=0) return -1;
    uintptr_t daddr=(uintptr_t)(f.x_dsdt ? f.x_dsdt : f.dsdt);
    const uint8_t *d=(const uint8_t*)daddr;
    if(!sdt_ok(d,36) || rd32(d)!=0x54445344UL) return -1; // "DSDT"
    uint32_t n=rd32(d+4);
    // Find AML NameOp `_S5_` and parse the following Package.
    for(uint32_t i=36;i+8<n;i++) {
        if(d[i]=='_' && d[i+1]=='S' && d[i+2]=='5' && d[i+3]=='_') {
            // Typical form: NameOp(0x08) + NameSeg + PackageOp(0x12) + PkgLen + Count + Int + Int
            uint32_t j=i;
            if(j==0 || d[j-1]!=0x08) continue;
            j+=4;
            if(j>=n || d[j]!=0x12) continue;
            j++;
            if(j>=n) continue;
            // Parse PkgLength encoding enough to skip to element count.
            uint8_t lead=d[j++]; uint32_t follow=(lead>>6)&3;
            uint32_t pkglen=lead&0x0F;
            for(uint32_t k=0;k<follow;k++){ if(j>=n)return -1; pkglen |= ((uint32_t)d[j++])<<(4+8*k); }
            (void)pkglen;
            if(j>=n) continue;
            uint32_t count=d[j++]; (void)count;
            uint32_t u=0; int x=aml_int(d+j,n-j,&u); if(x<0) continue; j+=u;
            int y=aml_int(d+j,n-j,&u); if(y<0) continue;
            *a=(uint16_t)x; *b=(uint16_t)y; return 0;
        }
    }
    return -1;
}

int poweroff_system(void) {
    uint16_t a=0,b=0;
    acpi_fadt_t f;
    if(get_fadt(&f)==0 && find_s5(&a,&b)==0 && f.pm1_cnt_len>=2) {
        uint16_t val=(uint16_t)(((a & 7u)<<10) | 0x2000u);
        if(f.pm1a_cnt && f.pm1a_cnt<=0xFFFFu) outw((uint16_t)f.pm1a_cnt,val);
        if(f.pm1b_cnt && f.pm1b_cnt<=0xFFFFu) outw((uint16_t)f.pm1b_cnt,val);
        // Give firmware/hypervisor a few milliseconds; normally control never returns.
        for(volatile uint32_t i=0;i<300000;i++) __asm__ volatile("pause");
        return -1;
    }
    // Compatibility fallback for QEMU/Bochs when ACPI discovery is unavailable.
    outw(0x604,0x2000);
    outw(0x4004,0x3400);
    return -1;
}

int reboot_system(void) {
    // ACPI RESET_REG, when present and byte-addressable.
    acpi_fadt_t f;
    if(get_fadt(&f)==0 && f.reset_reg_space==1 && f.reset_reg_port && f.reset_value) {
        outb(f.reset_reg_port, f.reset_value);
        for(volatile uint32_t i=0;i<300000;i++) __asm__ volatile("pause");
    }
    return -1;
}
