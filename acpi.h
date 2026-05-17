#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stddef.h>

#define PM1a_CNT_SLP_TYP_S5  (0x1 << 10)
#define PM1a_CNT_SLP_EN      (1 << 13)

struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct rsdt {
     struct acpi_header header;
     uint32_t table_ptrs[];
} __attribute__((packed));

struct rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct xsdt {
    struct acpi_header header;
    uint64_t table_ptrs[];
};

struct madt {
    struct acpi_header header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed));

struct mcfg {
    struct acpi_header header;
    uint64_t reserved;
    struct {
        uint64_t base_address;
        uint16_t pci_segment;
        uint8_t start_bus;
        uint8_t end_bus;
        uint32_t reserved;
    } config[];
} __attribute__((packed));

struct hpet {
    struct acpi_header header;
    uint8_t hardware_rev_id;
    uint8_t comparator_count;
    uint16_t pci_vendor_id;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } event_timer_block;
    uint8_t hpet_number;
    uint16_t min_tick;
    uint8_t page_protection;
} __attribute__((packed));

struct facp {
    struct acpi_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } reset_reg;
    uint8_t reset_value;
    uint16_t arm_boot_arch;
    uint8_t minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_pm1a_evt_blk;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_pm1b_evt_blk;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_pm1a_cnt_blk;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_pm1b_cnt_blk;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_pm2_cnt_blk;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_pm_tmr_blk;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_gpe0_blk;
    struct {
        uint8_t address_space;
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } x_gpe1_blk;
} __attribute__((packed));

extern struct facp* g_facp;
extern struct madt* g_madt;
extern struct mcfg* g_mcfg;
extern struct hpet* g_hpet;

void acpi_init(void);
void* acpi_find_table(const char* signature);
void acpi_reboot(void);
void acpi_poweroff(void);

#endif