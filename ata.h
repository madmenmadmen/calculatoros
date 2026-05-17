#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECTOR_SIZE     512

extern void ata_io_wait(void);
int ata_init(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);
int ata_init_drive(uint16_t io_base, uint8_t drive_sel);
uint32_t ata_get_sectors(void);

int ata_is_atapi_device(void);
int ata_can_write_device(void);

int ata_read_sectors_ex(uint16_t io_base, uint8_t slave, uint32_t lba, uint8_t count, void* buffer);
int ata_write_sectors_ex(uint16_t io_base, uint8_t slave, uint32_t lba, uint8_t count, const void* buffer);

#endif