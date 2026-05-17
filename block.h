#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

#define MAX_BLOCK_DEVICES 16
#define DEV_NAME_LEN 8

#define MBR_SIGNATURE 0xAA55
#define MAX_PARTITIONS 4

typedef struct {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    uint8_t bootstrap[446];
    mbr_partition_t parts[4];
    uint16_t signature;
} __attribute__((packed)) mbr_t;

typedef struct block_device {
    char name[DEV_NAME_LEN];
    int present;
    uint32_t sectors;

    uint16_t io_base;
    uint8_t slave;

    void* private;

    int (*read)(struct block_device*, uint32_t lba, uint8_t count, void* buf);
    int (*write)(struct block_device*, uint32_t lba, uint8_t count, const void* buf);

} block_device_t;

typedef struct {
    char name[8];
    block_device_t* parent;
    uint32_t start_lba;
    uint32_t sector_count;
    int present;
} partition_t;

extern block_device_t devices[MAX_BLOCK_DEVICES];
extern int dev_count;

void block_init(void);
block_device_t* block_find(const char* name);

extern block_device_t part_devices[MAX_PARTITIONS * MAX_BLOCK_DEVICES];
extern int part_count;
extern partition_t partitions[MAX_PARTITIONS * MAX_BLOCK_DEVICES];

int read_mbr(block_device_t* dev, mbr_t* mbr);
int create_mbr_partition(block_device_t* dev, int part_num, uint32_t start_sector, uint32_t size_mb);

void block_add_usb_device(int idx);
void block_remove_usb_device(int idx);

#endif