// ata.h - ATA PIO LBA28 磁盘读写 (主通道 0x1F0)
#ifndef ATA_H
#define ATA_H

#include <stdint.h>

// 成功返回 0, 失败返回 -1
int ata_read_sectors(uint32_t lba, uint8_t count, void* buf);
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buf);

// 探测主通道 ATA 设备是否存在; 返回 0 找到, -1 无设备/错误
int ata_probe(void);

// 磁盘信息 (ata_probe 成功后才有效; 未探测则返回安全默认值)
const char* ata_model(void);        // 型号字符串 (如 "VMware Virtual IDE Hard Drive")
uint64_t    ata_total_sectors(void); // 磁盘总扇区数 (LBA28/LBA48)


#endif // ATA_H
