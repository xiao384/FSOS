// ata.c - ATA PIO LBA28 磁盘读写 (主通道)
// 端口: 0x1F0-0x1F7, 轮询状态寄存器, 带超时保护
#include "ata.h"
#include "io.h"

#define ATA_DATA   0x1F0
#define ATA_ERR    0x1F1
#define ATA_SEC    0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_DRV    0x1F6
#define ATA_CMD    0x1F7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

// IDENTIFY 数据缓存 (供磁盘信息接口); ata_probe 成功后有效
static uint16_t g_ata_id[256];
static int      g_ata_ok = 0;

// 等待状态位匹配; 返回 0 成功, -1 错误, -2 超时
static int ata_wait(uint8_t mask, uint8_t val) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t st = inb(ATA_CMD);
        if (!(st & ATA_SR_BSY)) {
            if ((st & mask) == val) return 0;
            if (st & ATA_SR_ERR) return -1;
            return -2;
        }
    }
    return -2;
}

static void ata_select(uint32_t lba) {
    outb(ATA_DRV, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
}

int ata_read_sectors(uint32_t lba, uint8_t count, void* buf) {
    uint8_t* p = (uint8_t*)buf;
    for (int i = 0; i < count; i++) {
        ata_select(lba + i);
        outb(ATA_SEC, 1);
        outb(ATA_CMD, 0x20);          // READ SECTORS
        int r = ata_wait(ATA_SR_DRQ, ATA_SR_DRQ);
        if (r != 0) return -1;
        ins_words(ATA_DATA, p, 256);  // 512 bytes
        p += 512;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void* buf) {
    const uint8_t* p = (const uint8_t*)buf;
    for (int i = 0; i < count; i++) {
        ata_select(lba + i);
        outb(ATA_SEC, 1);
        outb(ATA_CMD, 0x30);          // WRITE SECTORS
        int r = ata_wait(ATA_SR_DRQ, ATA_SR_DRQ);
        if (r != 0) return -1;
        outs_words(ATA_DATA, p, 256); // 512 bytes
        p += 512;
    }
    // 等待写入完成
    int r = ata_wait(0, 0);
    if (r != 0) return -1;
    outb(ATA_CMD, 0xE7);              // FLUSH CACHE
    ata_wait(0, 0);
    return 0;
}

// 探测主通道 ATA 设备是否存在 (IDENTIFY DEVICE)
// 返回 0 找到设备, -1 无响应/错误
int ata_probe(void) {
    outb(ATA_DRV, 0xA0);
    outb(ATA_ERR, 0); outb(ATA_SEC, 0);
    outb(ATA_LBA0, 0); outb(ATA_LBA1, 0); outb(ATA_LBA2, 0);
    outb(ATA_CMD, 0xEC);             // IDENTIFY DEVICE
    uint8_t st = inb(ATA_CMD);
    if (st == 0) return -1;           // 无设备
    for (int i = 0; i < 2000000; i++) {
        st = inb(ATA_CMD);
        if (st & ATA_SR_BSY) continue;
        if (st & ATA_SR_ERR) return -1;
        if (st & ATA_SR_DRQ) {
            ins_words(ATA_DATA, (uint8_t*)g_ata_id, 256);
            g_ata_ok = 1;
            return 0;
        }
    }
    return -1;
}

// ---- 磁盘信息接口 ----
const char* ata_model(void) {
    static char m[41];
    if (!g_ata_ok) return "ATA Disk";
    // 型号存于 words 27..46 (20 个字, 每字两字节, 可能字节交换)
    for (int i = 0; i < 20; i++) {
        uint16_t w = g_ata_id[27 + i];
        m[i * 2]     = (char)(w & 0xFF);
        m[i * 2 + 1] = (char)(w >> 8);
    }
    m[40] = 0;
    int n = 40;
    while (n > 0 && (m[n - 1] == ' ' || m[n - 1] == 0)) n--;
    m[n] = 0;
    return m;
}

uint64_t ata_total_sectors(void) {
    if (!g_ata_ok) return 0;
    // 优先 LBA48 (word 83 bit2), 否则 LBA28 (words 60-61)
    if (g_ata_id[83] & (1u << 2)) {
        uint64_t lo = (uint64_t)g_ata_id[100] | ((uint64_t)g_ata_id[101] << 16);
        uint64_t hi = (uint64_t)g_ata_id[102] | ((uint64_t)g_ata_id[103] << 16);
        return lo | (hi << 32);
    }
    return ((uint64_t)g_ata_id[61] << 16) | g_ata_id[60];
}

