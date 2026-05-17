#include "block.h"
#include "ata.h"
#include "ahci.h"
#include "ehci.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

block_device_t devices[MAX_BLOCK_DEVICES];
int dev_count = 0;

partition_t partitions[MAX_PARTITIONS * MAX_BLOCK_DEVICES];
int part_count = 0;

block_device_t part_devices[MAX_PARTITIONS * MAX_BLOCK_DEVICES];

static int ahci_device_count = 0;

static int part_read_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, void* buf) {
    partition_t* part = (partition_t*)dev->private;
    return part->parent->read(part->parent, part->start_lba + lba, count, buf);
}

static int part_write_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, const void* buf) {
    partition_t* part = (partition_t*)dev->private;
    return part->parent->write(part->parent, part->start_lba + lba, count, buf);
}

static int ehci_usb_read_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, void* buf) {
    int idx = (int)(uintptr_t)dev->private;
    return ehci_usb_read(idx, lba, count, buf);
}

static int ehci_usb_write_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, const void* buf) {
    int idx = (int)(uintptr_t)dev->private;
    return ehci_usb_write(idx, lba, count, buf);
}

void add_partition(block_device_t* parent, int part_num, uint32_t start, uint32_t count) {
    partition_t* part = &partitions[part_count];
    strcpy(part->name, parent->name);

    int len = strlen(part->name);
    part->name[len] = '0' + part_num;
    part->name[len + 1] = 0;

    part->parent = parent;
    part->start_lba = start;
    part->sector_count = count;
    part->present = 1;

    block_device_t* dev = &part_devices[part_count];
    strcpy(dev->name, part->name);
    dev->present = 1;
    dev->sectors = count;
    dev->io_base = parent->io_base;
    dev->slave = parent->slave;
    dev->private = part;
    dev->read = part_read_wrapper;
    dev->write = part_write_wrapper;

    part_count++;
}

static int ata_read_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, void* buf) {
    return ata_read_sectors_ex(dev->io_base, dev->slave, lba, count, buf);
}

static int ata_write_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, const void* buf) {
    return ata_write_sectors_ex(dev->io_base, dev->slave, lba, count, buf);
}

static int ahci_read_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, void* buf) {
    int port = (int)(uintptr_t)dev->private;
    return ahci_read(port, lba, count, buf);
}

static int ahci_write_wrapper(block_device_t* dev, uint32_t lba, uint8_t count, const void* buf) {
    int port = (int)(uintptr_t)dev->private;
    return ahci_write(port, lba, count, buf);
}

static int ata_drive_exists(uint16_t io_base, uint8_t drive_sel) {
    outb(io_base + 6, drive_sel);
    ata_io_wait();

    uint8_t status = inb(io_base + 7);

    if (status == 0xFF) return 0;

    uint8_t lba_mid = inb(io_base + 4);
    uint8_t lba_hi = inb(io_base + 5);

    if (lba_mid == 0xFF && lba_hi == 0xFF) return 0;

    return 1;
}

static uint16_t last_ata_io = 0;
static uint8_t last_ata_slave = 0;

void block_init(void) {
    dev_count = 0;
    ahci_device_count = 0;
    int dvd_index = 0;

    if (ata_drive_exists(0x1F0, 0xA0) && ata_init_drive(0x1F0, 0)) {
        block_device_t* dev = &devices[dev_count++];
        strcpy(dev->name, "hda");
        dev->present = 1;
        dev->sectors = ata_get_sectors();
        dev->io_base = 0x1F0;
        dev->slave = 0;
        dev->read = ata_read_wrapper;
        dev->write = ata_write_wrapper;
        last_ata_io = 0x1F0;
        last_ata_slave = 0;
    }

    if (ata_drive_exists(0x1F0, 0xB0) && ata_init_drive(0x1F0, 1)) {
        block_device_t* dev = &devices[dev_count++];
        strcpy(dev->name, "hdb");
        dev->present = 1;
        dev->sectors = ata_get_sectors();
        dev->io_base = 0x1F0;
        dev->slave = 1;
        dev->read = ata_read_wrapper;
        dev->write = ata_write_wrapper;
    }

    if (ata_drive_exists(0x170, 0xA0) && ata_init_drive(0x170, 0)) {
        block_device_t* dev = &devices[dev_count++];
        strcpy(dev->name, "hdc");
        dev->present = 1;
        dev->sectors = ata_get_sectors();
        dev->io_base = 0x170;
        dev->slave = 0;
        dev->read = ata_read_wrapper;
        dev->write = ata_write_wrapper;
        last_ata_io = 0x170;
        last_ata_slave = 0;
    }

    if (ata_drive_exists(0x170, 0xB0) && ata_init_drive(0x170, 1)) {
        block_device_t* dev = &devices[dev_count++];
        strcpy(dev->name, "hdd");
        dev->present = 1;
        dev->sectors = ata_get_sectors();
        dev->io_base = 0x170;
        dev->slave = 1;
        dev->read = ata_read_wrapper;
        dev->write = ata_write_wrapper;
    }

    ehci_init();

    for (int i = 0; i < ehci_usb_get_count() && dev_count < MAX_BLOCK_DEVICES; i++) {
        block_device_t* dev = &devices[dev_count++];
        char name[5] = "usb";
        name[3] = 'a' + i;
        name[4] = '\0';
        strcpy(dev->name, name);
        dev->present = 1;

        uint32_t sectors, bs;
        ehci_usb_get_info(i, &sectors, &bs);
        dev->sectors = sectors;
        dev->private = (void*)(uintptr_t)i;
        dev->read = ehci_usb_read_wrapper;
        dev->write = ehci_usb_write_wrapper;
    }

    for (int i = 0; i < dev_count; i++) {
        mbr_t mbr;
        if (read_mbr(&devices[i], &mbr) && mbr.signature == MBR_SIGNATURE) {
            for (int j = 0; j < 4; j++) {
                if (mbr.parts[j].type != 0) {
                    add_partition(&devices[i], j + 1,
                        mbr.parts[j].lba_start,
                        mbr.parts[j].sector_count);
                }
            }
        }
    }

    ahci_init();

    int ahci_ports = ahci_get_port_count();
    int first_ahci_index = dev_count;

    for (int i = 0; i < ahci_ports && i < 4 && dev_count < MAX_BLOCK_DEVICES; i++) {
        block_device_t* dev = &devices[dev_count++];

        char name[5];
        name[0] = 's';
        name[1] = 'd';
        name[2] = 'a' + i;
        name[3] = '\0';

        strcpy(dev->name, name);
        dev->present = 1;
        dev->sectors = 0;
        dev->io_base = 0;
        dev->slave = 0;
        dev->private = (void*)(uintptr_t)i;
        dev->read = ahci_read_wrapper;
        dev->write = ahci_write_wrapper;

        ahci_device_count++;
    }

    for (int i = first_ahci_index; i < dev_count; i++) {
        mbr_t mbr;
        if (read_mbr(&devices[i], &mbr) && mbr.signature == MBR_SIGNATURE) {
            for (int j = 0; j < 4; j++) {
                if (mbr.parts[j].type != 0) {
                    add_partition(&devices[i], j + 1,
                        mbr.parts[j].lba_start,
                        mbr.parts[j].sector_count);
                }
            }
        }
    }
}

block_device_t* block_find(const char* name) {
    for (int i = 0; i < dev_count; i++) {
        if (streq(devices[i].name, name)) {
            return &devices[i];
        }
    }

    for (int i = 0; i < part_count; i++) {
        if (streq(partitions[i].name, name)) {
            return &part_devices[i];
        }
    }
    return 0;
}

int read_mbr(block_device_t* dev, mbr_t* mbr) {
    return dev->read(dev, 0, 1, mbr);
}

block_device_t* partition_find(const char* name) {
    for (int i = 0; i < part_count; i++) {
        if (streq(partitions[i].name, name)) {
            return &part_devices[i];
        }
    }
    return 0;
}

int create_mbr_partition(block_device_t* dev, int part_num, uint32_t start_sector, uint32_t size_mb) {
    if (part_num < 1 || part_num > 4) {
        print("Partition number must be 1-4\n");
        return 0;
    }

    uint32_t size_sectors = size_mb * 2048;

    mbr_t mbr;
    if (!dev->read(dev, 0, 1, &mbr)) {
        print("Failed to read MBR\n");
        return 0;
    }

    if (mbr.parts[part_num - 1].type != 0) {
        print("Partition ");
        print_hex(part_num);
        print(" already exists (type: ");
        print_hex(mbr.parts[part_num - 1].type);
        print(")\n");
        return 0;
    }

    for (int i = 0; i < 4; i++) {
        if (mbr.parts[i].type != 0) {
            uint32_t part_start = mbr.parts[i].lba_start;
            uint32_t part_end = part_start + mbr.parts[i].sector_count;

            if ((start_sector >= part_start && start_sector < part_end) ||
                (start_sector + size_sectors > part_start && start_sector + size_sectors <= part_end) ||
                (start_sector <= part_start && start_sector + size_sectors >= part_end)) {
                print("Error: New partition overlaps with existing partition ");
                print_hex(i + 1);
                print("\n");
                return 0;
            }
        }
    }

    mbr.parts[part_num - 1].status = 0x00;
    mbr.parts[part_num - 1].chs_first[0] = 0;
    mbr.parts[part_num - 1].chs_first[1] = 0;
    mbr.parts[part_num - 1].chs_first[2] = 0;
    mbr.parts[part_num - 1].type = 0x83;
    mbr.parts[part_num - 1].chs_last[0] = 0;
    mbr.parts[part_num - 1].chs_last[1] = 0;
    mbr.parts[part_num - 1].chs_last[2] = 0;
    mbr.parts[part_num - 1].lba_start = start_sector;
    mbr.parts[part_num - 1].sector_count = size_sectors;

    mbr.signature = MBR_SIGNATURE;

    if (!dev->write(dev, 0, 1, &mbr)) {
        print("Failed to write MBR\n");
        return 0;
    }

    print("Partition ");
    print_hex(part_num);
    print(" created: start=");
    print_hex(start_sector);
    print(", size=");
    print_hex(size_mb);
    print(" MB\n");

    if (read_mbr(dev, &mbr) && mbr.signature == MBR_SIGNATURE) {
        for (int j = 0; j < 4; j++) {
            if (mbr.parts[j].type != 0) {
                int exists = 0;
                for (int k = 0; k < part_count; k++) {
                    if (partitions[k].parent == dev &&
                        partitions[k].start_lba == mbr.parts[j].lba_start) {
                        exists = 1;
                        break;
                    }
                }
                if (!exists) {
                    add_partition(dev, j + 1,
                        mbr.parts[j].lba_start,
                        mbr.parts[j].sector_count);
                }
            }
        }
    }

    return 1;
}

void update_partitions_and_dev(void) {
    int new_part_count = 0;

    for (int i = 0; i < dev_count; i++) {
        mbr_t mbr;
        if (read_mbr(&devices[i], &mbr) && mbr.signature == MBR_SIGNATURE) {
            for (int j = 0; j < 4; j++) {
                if (mbr.parts[j].type != 0) {
                    partition_t* part = &partitions[new_part_count];
                    strcpy(part->name, devices[i].name);

                    int len = strlen(part->name);
                    part->name[len] = '0' + (j + 1);
                    part->name[len + 1] = 0;

                    part->parent = &devices[i];
                    part->start_lba = mbr.parts[j].lba_start;
                    part->sector_count = mbr.parts[j].sector_count;
                    part->present = 1;

                    block_device_t* dev = &part_devices[new_part_count];
                    strcpy(dev->name, part->name);
                    dev->present = 1;
                    dev->sectors = mbr.parts[j].sector_count;
                    dev->io_base = devices[i].io_base;
                    dev->slave = devices[i].slave;
                    dev->private = part;
                    dev->read = part_read_wrapper;
                    dev->write = part_write_wrapper;

                    new_part_count++;
                }
            }
        }
    }

    part_count = new_part_count;
}

int delete_mbr_partition(block_device_t* dev, int part_num) {
    if (part_num < 1 || part_num > 4) {
        print("Partition number must be 1-4\n");
        return 0;
    }

    mbr_t mbr;
    if (!dev->read(dev, 0, 1, &mbr)) {
        print("Failed to read MBR\n");
        return 0;
    }

    if (mbr.parts[part_num - 1].type == 0) {
        print("Partition ");
        print_hex(part_num);
        print(" does not exist\n");
        return 0;
    }

    mbr.parts[part_num - 1].status = 0;
    mbr.parts[part_num - 1].chs_first[0] = 0;
    mbr.parts[part_num - 1].chs_first[1] = 0;
    mbr.parts[part_num - 1].chs_first[2] = 0;
    mbr.parts[part_num - 1].type = 0;
    mbr.parts[part_num - 1].chs_last[0] = 0;
    mbr.parts[part_num - 1].chs_last[1] = 0;
    mbr.parts[part_num - 1].chs_last[2] = 0;
    mbr.parts[part_num - 1].lba_start = 0;
    mbr.parts[part_num - 1].sector_count = 0;

    if (!dev->write(dev, 0, 1, &mbr)) {
        print("Failed to write MBR\n");
        return 0;
    }

    print("Partition ");
    print_hex(part_num);
    print(" deleted\n");

    update_partitions_and_dev();

    return 1;
}

void block_add_usb_device(int idx) {
    if (dev_count >= MAX_BLOCK_DEVICES) {
        print("block: no space for new USB device\n");
        return;
    }

    block_device_t* dev = &devices[dev_count++];
    char name[5] = "usb";
    name[3] = 'a' + (idx);
    name[4] = '\0';
    strcpy(dev->name, name);
    dev->present = 1;

    uint32_t sectors, bs;
    ehci_usb_get_info(idx, &sectors, &bs);
    dev->sectors = sectors;
    dev->private = (void*)(uintptr_t)idx;
    dev->read = ehci_usb_read_wrapper;
    dev->write = ehci_usb_write_wrapper;

    mbr_t mbr;
    if (read_mbr(dev, &mbr) && mbr.signature == MBR_SIGNATURE) {
        for (int j = 0; j < 4; j++) {
            if (mbr.parts[j].type != 0) {
                add_partition(dev, j + 1,
                    mbr.parts[j].lba_start,
                    mbr.parts[j].sector_count);
            }
        }
    }

    fs_update_devices();

    print("USB device added: ");
    print(name);
    print("\n");
}

void block_remove_usb_device(int idx) {
    char name[5] = "usb";
    name[3] = 'a' + idx;
    name[4] = '\0';

    for (int i = 0; i < part_count; i++) {
        if (strncmp(partitions[i].name, name, 4) == 0) {

            for (int j = i; j < part_count - 1; j++) {
                partitions[j] = partitions[j + 1];
                part_devices[j] = part_devices[j + 1];
            }
            part_count--;
            i--;
        }
    }

    for (int i = 0; i < dev_count; i++) {
        if (streq(devices[i].name, name)) {
            for (int j = i; j < dev_count - 1; j++) {
                devices[j] = devices[j + 1];
            }
            dev_count--;
            break;
        }
    }

    fs_update_devices();

    print("USB device removed: ");
    print(name);
    print("\n");
}