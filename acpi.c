#include "acpi.h"
#include "io.h"

struct madt* g_madt = NULL;
struct mcfg* g_mcfg = NULL;
struct hpet* g_hpet = NULL;
struct facp* g_facp = NULL;

static uint8_t checksum(void* addr, uint32_t length) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++)
        sum += ((uint8_t*)addr)[i];
    return sum == 0;
}

static struct rsdp* find_rsdp(void) {

    uint16_t ebda_seg = *(uint16_t*)0x40E;

    if (ebda_seg) {
        struct rsdp* rsdp = (struct rsdp*)((uint32_t)ebda_seg << 4);

        if (rsdp->signature[0] == 'R' && memcmp(rsdp->signature, "RSD PTR ", 8) == 0) {
            if (checksum(rsdp, 20)) {
                return rsdp;
            }
        }
    }

    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        struct rsdp* rsdp = (struct rsdp*)addr;

        if (rsdp->signature[0] == 'R' && memcmp(rsdp->signature, "RSD PTR ", 8) == 0) {
            if (checksum(rsdp, 20)) {
                return rsdp;
            }
        }
    }

    print("  RSDP not found!\n");
    return NULL;
}

void* acpi_find_table(const char* signature) {
    struct rsdp* rsdp = find_rsdp();
    if (!rsdp) {
        print("  No RSDP found!\n");
        return NULL;
    }

    if (!rsdp->rsdt_address) {
        print("  RSDT is NULL!\n");
        return NULL;
    }

    struct rsdt* rsdt = (struct rsdt*)(uint32_t)rsdp->rsdt_address;

    int entries = (rsdt->header.length - sizeof(rsdt->header)) / 4;

    for (int i = 0; i < entries; i++) {
        uint32_t table_addr = rsdt->table_ptrs[i];
        struct acpi_header* hdr = (struct acpi_header*)(uint32_t)table_addr;

        if (memcmp(hdr->signature, signature, 4) == 0) {
            if (checksum(hdr, hdr->length)) {
                return hdr;
            }
            else {
                print("    Checksum FAILED!\n");
            }
        }
    }

    print("  Table ");
    print(signature);
    print(" not found!\n");
    return NULL;
}

void acpi_reboot(void) {
    if (g_facp && g_facp->reset_reg.address) {

        uint64_t addr = g_facp->reset_reg.address;
        uint8_t value = g_facp->reset_value;

        switch (g_facp->reset_reg.address_space) {
        case 0:
            *(volatile uint8_t*)(uint32_t)addr = value;
            break;
        case 1:
            outb(addr, value);
            break;
        case 2:
            break;
        }

        for (;;);
    }

    __asm__ volatile ("cli");
    outb(0x64, 0xFE);
    for (;;);
}

void acpi_poweroff(void) {
    if (!g_facp) {
        print("ACPI: No FADT, cannot poweroff\n");
        return;
    }

    uint32_t pm1a_cnt_addr = 0;
    uint32_t pm1a_evt_addr = 0;
    uint8_t pm1_cnt_len = 0;

    if (g_facp->pm1a_cnt_blk) {
        pm1a_cnt_addr = g_facp->pm1a_cnt_blk;
        pm1a_evt_addr = g_facp->pm1a_evt_blk;
        pm1_cnt_len = g_facp->pm1_cnt_len;
    }

    else if (g_facp->x_pm1a_cnt_blk.address) {
        pm1a_cnt_addr = (uint32_t)g_facp->x_pm1a_cnt_blk.address;
        pm1a_evt_addr = (uint32_t)g_facp->x_pm1a_evt_blk.address;
        pm1_cnt_len = g_facp->pm1_cnt_len;
    }

    if (!pm1a_cnt_addr || pm1_cnt_len < 2) {
        print("ACPI: PM1a_CNT not available\n");
        return;
    }

    uint16_t pm1a_cnt = inw(pm1a_cnt_addr);

    pm1a_cnt &= ~(0x7 << 10);
    pm1a_cnt &= ~(1 << 13);

    pm1a_cnt |= (0x7 << 10);
    pm1a_cnt |= (1 << 13);

    outw(pm1a_cnt_addr, pm1a_cnt);

    for (volatile int i = 0; i < 1000000; i++);

    pm1a_cnt = inw(pm1a_cnt_addr);
    pm1a_cnt &= ~(0x7 << 10);
    pm1a_cnt &= ~(1 << 13);
    pm1a_cnt |= (0x1 << 10);
    pm1a_cnt |= (1 << 13);
    outw(pm1a_cnt_addr, pm1a_cnt);

    for (volatile int i = 0; i < 1000000; i++);

    acpi_reboot();
}

void acpi_init(void) {
    g_madt = (struct madt*)acpi_find_table("APIC");
    g_mcfg = (struct mcfg*)acpi_find_table("MCFG");
    g_hpet = (struct hpet*)acpi_find_table("HPET");
    g_facp = (struct facp*)acpi_find_table("FACP");
}