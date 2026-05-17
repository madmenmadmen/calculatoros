#include "ata.h"
#include "io.h"
#include <stdint.h>

#define ATA_REG_DATA       0
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LO     3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HI     5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static uint16_t ata_io = 0x1F0;
static uint16_t ata_ctrl = 0x3F6;
static uint8_t ata_slave = 0;
static uint32_t ata_sectors = 0;
static int ata_present = 0;

static uint8_t ata_status(void) {
    return inb(ata_io + ATA_REG_STATUS);
}

static int ata_wait_bsy(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(ata_status() & ATA_SR_BSY))
            return 1;
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t s = ata_status();

        if (s & ATA_SR_ERR) return 0;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ))
            return 1;
    }
    return 0;
}

void ata_io_wait(void) {
    inb(ata_ctrl);
    inb(ata_ctrl);
    inb(ata_ctrl);
    inb(ata_ctrl);
}

static int ata_identify(void) {

    outb(ata_io + ATA_REG_SECCOUNT, 0);
    outb(ata_io + ATA_REG_LBA_LO, 0);
    outb(ata_io + ATA_REG_LBA_MID, 0);
    outb(ata_io + ATA_REG_LBA_HI, 0);

    outb(ata_io + ATA_REG_COMMAND, 0xEC);

    uint8_t status = ata_status();
    if (status == 0) return 0;

    if (!ata_wait_bsy()) return 0;

    while (!(status & ATA_SR_DRQ)) {
        if (status & ATA_SR_ERR) return 0;
        status = ata_status();
    }

    uint16_t buf[256];
    for (int i = 0; i < 256; i++) {
        buf[i] = inw(ata_io + ATA_REG_DATA);
    }

    ata_sectors = buf[60] | (buf[61] << 16);
    return 1;
}

int ata_init_drive(uint16_t io_base, uint8_t slave) {

    ata_io = io_base;
    ata_ctrl = (io_base == 0x1F0) ? 0x3F6 : 0x376;
    ata_slave = slave;

    outb(ata_io + ATA_REG_DRIVE, 0xA0 | (slave << 4));
    ata_io_wait();

    if (!ata_identify()) {
        ata_present = 0;
        return 0;
    }

    ata_present = 1;
    return 1;
}

uint32_t ata_get_sectors(void) {
    return ata_sectors;
}

static void ata_io_wait_ex(uint16_t ctrl) {
    inb(ctrl);
    inb(ctrl);
    inb(ctrl);
    inb(ctrl);
}

static void ata_select_ex(uint16_t io, uint16_t ctrl, uint8_t slave, uint32_t lba) {
    outb(io + 6, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    ata_io_wait_ex(ctrl);
}

int ata_read_sectors_ex(uint16_t io, uint8_t slave,
    uint32_t lba, uint8_t count, void* buffer)
{
    uint16_t* buf = (uint16_t*)buffer;

    for (int i = 0; i < count; i++) {

        uint32_t sector = lba + i;

        uint16_t ctrl = (io == 0x1F0) ? 0x3F6 : 0x376;
        ata_select_ex(io, ctrl, slave, sector);

        while (inb(io + 7) & ATA_SR_BSY);

        outb(io + 2, 1);
        outb(io + 3, sector);
        outb(io + 4, sector >> 8);
        outb(io + 5, sector >> 16);

        outb(io + 7, 0x20);
        ata_io_wait_ex(ctrl);

        uint8_t status;
        while (1) {
            status = inb(io + 7);
            if (status & ATA_SR_ERR) return 0;
            if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
                break;
        }

        for (int j = 0; j < 256; j++) {
            buf[j + i * 256] = inw(io);
        }
    }

    return 1;
}

int ata_write_sectors_ex(uint16_t io, uint8_t slave,
    uint32_t lba, uint8_t count, const void* buffer)
{
    const uint16_t* buf = (const uint16_t*)buffer;

    for (int i = 0; i < count; i++) {

        uint32_t sector = lba + i;

        uint16_t ctrl = (io == 0x1F0) ? 0x3F6 : 0x376;
        ata_select_ex(io, ctrl, slave, sector);

        outb(io + 2, 1);
        outb(io + 3, sector);
        outb(io + 4, sector >> 8);
        outb(io + 5, sector >> 16);

        outb(io + 7, 0x30);
        ata_io_wait_ex(ctrl);

        uint8_t status;
        while (1) {
            status = inb(io + 7);
            if (status & ATA_SR_ERR) return 0;
            if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
                break;
        }

        for (int j = 0; j < 256; j++) {
            outw(io, *buf++);
        }

        outb(io + 7, 0xE7);
        while (inb(io + 7) & ATA_SR_BSY);
    }

    return 1;
}