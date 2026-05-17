#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>

#define HBA_CAP_S64A    (1 << 31)
#define HBA_CAP_NCS     ((0x1F << 8) & 0x1F00)

#define HBA_CAP         0x00
#define HBA_GHC         0x04
#define HBA_IS          0x08
#define HBA_PI          0x0C
#define HBA_VS          0x10

#define PORT_CLB        0x00
#define PORT_CLBU       0x04
#define PORT_FB         0x08
#define PORT_FBU        0x0C
#define PORT_IS         0x10
#define PORT_IE         0x14
#define PORT_CMD        0x18
#define PORT_TFD        0x20
#define PORT_SIG        0x24
#define PORT_SSTS       0x28
#define PORT_SCTL       0x2C
#define PORT_SERR       0x30
#define PORT_SACT       0x34
#define PORT_CI         0x38
#define PORT_IS         0x10

#define PXCMD_ST        0x0001
#define PXCMD_FRE       0x0010
#define PXCMD_SUD       0x0002
#define PXCMD_POD       0x0004
#define PXCMD_CR        0x8000

#define PXSTS_DET       0x000F
#define PXSTS_DET_PRESENT 0x0001
#define PXSTS_DET_PHY_ON 0x0003
#define PXSTS_IPM       0x0F00
#define PXSTS_SPD       0x00F0

#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_IDENTIFY        0xEC

#define FIS_TYPE_H2D     0x27
#define FIS_TYPE_D2H     0x34
#define FIS_TYPE_DMA_SETUP 0x41
#define FIS_TYPE_PIO_SETUP 0x5F

#define CMD_HDR_CFL(cfl)      ((cfl) & 0x1F)
#define CMD_HDR_WRITE(write)  (((write) & 1) << 6)
#define CMD_HDR_PRDTL(prdtl)  (((prdtl) & 0xFFFF) << 16)

#define AHCI_MAX_TRANSFER 65536

#define MAX_PORTS 32
#define MAX_CMD_SLOTS 32

#define AHCI_IRQ 11

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsvd;
    uint32_t dbc : 22;
    uint32_t rsvd1 : 9;
    uint32_t i : 1;
} __attribute__((packed)) ahci_prd_t;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[32];
    uint8_t rsvd[32];
    ahci_prd_t prdt[0];
} __attribute__((packed)) ahci_cmd_table_t;

typedef struct {
    uint32_t cfl : 5;
    uint32_t atapi : 1;
    uint32_t write : 1;
    uint32_t prefetch : 1;
    uint32_t reset : 1;
    uint32_t bist : 1;
    uint32_t clear : 1;
    uint32_t rsvd : 1;
    uint32_t pmp : 4;
    uint32_t prdtl : 16;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsvd1[4];
} __attribute__((packed)) ahci_cmd_hdr_t;

typedef struct {
    uint8_t fis_type;
    uint8_t pm_port : 4;
    uint8_t rsvd0 : 3;
    uint8_t c : 1;
    uint8_t command;
    uint8_t feature_low;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_high;
    uint8_t count_low;
    uint8_t count_high;
    uint8_t icc;
    uint8_t control;
    uint32_t rsvd1;
} __attribute__((packed)) ahci_h2d_fis_t;

typedef struct {
    uint8_t d2h_fis[20];
    uint8_t pio_fis[20];
    uint8_t dma_fis[20];
    uint8_t sdb_fis[20];
    uint8_t unknown[64];
    uint8_t rsvd[112];
} __attribute__((packed)) ahci_fis_t;

typedef struct {
    ahci_cmd_hdr_t* cmd_list;
    ahci_fis_t* fis;
    ahci_cmd_table_t* cmd_table[MAX_CMD_SLOTS];
    int cmd_table_allocated[MAX_CMD_SLOTS];
    int initialized;
} ahci_port_cache_t;

void ahci_init(void);
void ahci_irq_handler(int port);
int ahci_get_port_count(void);
int ahci_read(int port, uint32_t lba, uint8_t count, void* buf);
int ahci_write(int port, uint32_t lba, uint8_t count, const void* buf);
volatile uint32_t* port_reg(int port, uint32_t offset);

#endif