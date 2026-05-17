#include "ahci.h"
#include "pci.h"
#include "io.h"
#include "pic.h"
#include "mmio.h"
#include <stddef.h>

#define TIMEOUT 1000000

static uintptr_t ahci_bar = 0;
static int port_count = 0;
static int ports_impl = 0;
static volatile int cmd_complete[MAX_PORTS][MAX_CMD_SLOTS] = { {0} };

static ahci_cmd_table_t* cmd_table_cache[MAX_PORTS][MAX_CMD_SLOTS] = { {0} };

static void ahci_udelay(int us) {
    for (volatile int i = 0; i < us * 10; i++);
}

volatile uint32_t* ahci_reg(uint32_t offset) {
    return (volatile uint32_t*)(ahci_bar + offset);
}

volatile uint32_t* port_reg(int port, uint32_t offset) {
    return (volatile uint32_t*)(ahci_bar + 0x100 + (port * 0x80) + offset);
}

static void ahci_delay(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++);
}

int ahci_get_port_count(void) {
    return port_count;
}

static int ahci_find_controller(void) {
    uint8_t bus, dev, func;

    if (!pci_find_class(0x010601, &bus, &dev, &func)) {
        return 0;
    }

    uint32_t bar5 = pci_read(bus, dev, func, 0x24);
    ahci_bar = bar5 & ~0x3FF;

    uint16_t cmd = pci_read(bus, dev, func, 0x04) & 0xFFFF;
    cmd |= (1 << 0) | (1 << 2);
    pci_write(bus, dev, func, 0x04, cmd);

    return 1;
}

static int ahci_port_detect(int port) {
    uint32_t ssts = mmio_read(port_reg(port, PORT_SSTS));
    uint32_t det = ssts & PXSTS_DET;
    return (det == PXSTS_DET_PHY_ON);
}

static int ahci_stop_port(int port) {
    uint32_t cmd = mmio_read(port_reg(port, PORT_CMD));
    if (cmd & PXCMD_ST) {
        mmio_write(port_reg(port, PORT_CMD), cmd & ~PXCMD_ST);
        for (int i = 0; i < TIMEOUT; i++) {
            if (!(mmio_read(port_reg(port, PORT_CMD)) & PXCMD_CR))
                break;
            ahci_delay(1);
        }
    }
    return 1;
}

static int ahci_start_port(int port) {
    uint32_t cmd = mmio_read(port_reg(port, PORT_CMD));
    mmio_write(port_reg(port, PORT_CMD), cmd | PXCMD_ST);
    return 1;
}

static int ahci_port_init(int port) {
    ahci_stop_port(port);

    void* cmd_list = alloc_phys(1);
    void* fis_base = alloc_phys(1);

    if (!cmd_list || !fis_base) return 0;

    memset((void*)cmd_list, 0, 4096);
    memset((void*)fis_base, 0, 4096);

    mmio_write(port_reg(port, PORT_CLB), (uint32_t)cmd_list);
    mmio_write(port_reg(port, PORT_CLBU), 0);
    mmio_write(port_reg(port, PORT_FB), (uint32_t)fis_base);
    mmio_write(port_reg(port, PORT_FBU), 0);

    uint32_t cmd = mmio_read(port_reg(port, PORT_CMD));
    mmio_write(port_reg(port, PORT_CMD), cmd | PXCMD_FRE);

    mmio_write(port_reg(port, PORT_SERR), 0xFFFFFFFF);

    for (int i = 0; i < MAX_CMD_SLOTS; i++) {
        if (!cmd_table_cache[port][i]) {
            cmd_table_cache[port][i] = (ahci_cmd_table_t*)alloc_phys(1);
            if (cmd_table_cache[port][i]) {
                memset((void*)cmd_table_cache[port][i], 0, 4096);
            }
        }
    }

    ahci_start_port(port);
    return 1;
}

static int ahci_wait_for_bsy(int port) {
    for (int i = 0; i < 100000; i++) {
        uint32_t tfd = mmio_read(port_reg(port, PORT_TFD));
        if (!(tfd & 0x80)) return 1;
        ahci_udelay(10);
    }
    return 0;
}

static int ahci_exec_command_poll(int port, int slot) {
    if (!ahci_wait_for_bsy(port)) return 0;
    __asm__ volatile ("sfence" ::: "memory");
    mmio_write(port_reg(port, PORT_CI), 1 << slot);
    for (int i = 0; i < 500000; i++) {
        uint32_t ci = mmio_read(port_reg(port, PORT_CI));
        if (!(ci & (1 << slot))) {
            uint32_t tfd = mmio_read(port_reg(port, PORT_TFD));
            if (tfd & 0x01) return 0;
            return 1;
        }
        ahci_udelay(10);
    }
    return 0;
}

int ahci_read(int port, uint32_t lba, uint8_t count, void* buf) {
    if (port >= port_count) return 0;

    int slot = 0;
    uint32_t ci = mmio_read(port_reg(port, PORT_CI));
    for (; slot < MAX_CMD_SLOTS; slot++) {
        if (!(ci & (1 << slot))) break;
    }
    if (slot >= MAX_CMD_SLOTS) return 0;

    uint32_t clb = mmio_read(port_reg(port, PORT_CLB));
    ahci_cmd_hdr_t* hdr = (ahci_cmd_hdr_t*)(uintptr_t)(clb + (slot * 32));

    if (!cmd_table_cache[port][slot]) return 0;
    ahci_cmd_table_t* cmd_table = cmd_table_cache[port][slot];

    cmd_table->prdt[0].dba = (uint32_t)buf;
    cmd_table->prdt[0].dbau = 0;
    cmd_table->prdt[0].dbc = (count * 512) - 1;
    cmd_table->prdt[0].i = 1;

    hdr->cfl = sizeof(ahci_h2d_fis_t) / 4;
    hdr->write = 0;
    hdr->prdtl = 1;
    hdr->ctba = (uint32_t)cmd_table;
    hdr->ctbau = 0;

    ahci_h2d_fis_t* fis = (ahci_h2d_fis_t*)cmd_table->cfis;
    memset(fis, 0, sizeof(ahci_h2d_fis_t));

    fis->fis_type = FIS_TYPE_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_READ_DMA_EXT;
    fis->lba0 = (lba >> 0) & 0xFF;
    fis->lba1 = (lba >> 8) & 0xFF;
    fis->lba2 = (lba >> 16) & 0xFF;
    fis->device = 0x40;
    fis->lba3 = (lba >> 24) & 0xFF;
    fis->count_low = count;
    fis->count_high = count >> 8;

    return ahci_exec_command_poll(port, slot);
}

int ahci_write(int port, uint32_t lba, uint8_t count, const void* buf) {
    if (port >= port_count) return 0;

    int slot = 0;
    uint32_t ci = mmio_read(port_reg(port, PORT_CI));
    for (; slot < MAX_CMD_SLOTS; slot++) {
        if (!(ci & (1 << slot))) break;
    }
    if (slot >= MAX_CMD_SLOTS) return 0;

    uint32_t clb = mmio_read(port_reg(port, PORT_CLB));
    ahci_cmd_hdr_t* hdr = (ahci_cmd_hdr_t*)(uintptr_t)(clb + (slot * 32));

    if (!cmd_table_cache[port][slot]) return 0;
    ahci_cmd_table_t* cmd_table = cmd_table_cache[port][slot];

    cmd_table->prdt[0].dba = (uint32_t)buf;
    cmd_table->prdt[0].dbau = 0;
    cmd_table->prdt[0].dbc = (count * 512) - 1;
    cmd_table->prdt[0].i = 1;

    hdr->cfl = sizeof(ahci_h2d_fis_t) / 4;
    hdr->write = 1;
    hdr->prdtl = 1;
    hdr->ctba = (uint32_t)cmd_table;
    hdr->ctbau = 0;

    ahci_h2d_fis_t* fis = (ahci_h2d_fis_t*)cmd_table->cfis;
    memset(fis, 0, sizeof(ahci_h2d_fis_t));

    fis->fis_type = FIS_TYPE_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_WRITE_DMA_EXT;
    fis->lba0 = (lba >> 0) & 0xFF;
    fis->lba1 = (lba >> 8) & 0xFF;
    fis->lba2 = (lba >> 16) & 0xFF;
    fis->device = 0x40;
    fis->lba3 = (lba >> 24) & 0xFF;
    fis->count_low = count;
    fis->count_high = count >> 8;

    return ahci_exec_command_poll(port, slot);
}

void ahci_init(void) {
    if (!ahci_find_controller()) {
        print("AHCI: controller not found\n");
        return;
    }

    mmio_write(ahci_reg(HBA_GHC), 0x80000000);
    ahci_delay(10);

    ports_impl = mmio_read(ahci_reg(HBA_PI));

    for (int port = 0; port < MAX_PORTS; port++) {
        if (ports_impl & (1 << port)) {
            if (ahci_port_detect(port)) {
                if (ahci_port_init(port)) {
                    port_count++;
                }
            }
        }
    }
}