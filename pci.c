#include "pci.h"
#include "io.h"

uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | ((dev & 0x1F) << 11) |
        ((func & 0x07) << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | (bus << 16) | ((dev & 0x1F) << 11) |
        ((func & 0x07) << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) |
        ((bus & 0xFF) << 16) |
        ((dev & 0x1F) << 11) |
        ((func & 0x07) << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) |
        ((bus & 0xFF) << 16) |
        ((dev & 0x1F) << 11) |
        ((func & 0x07) << 8) |
        (offset & 0xFC);

    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

int pci_find_class(uint32_t class_code, uint8_t* out_bus, uint8_t* out_dev, uint8_t* out_func) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t class_rev = pci_read_config(bus, dev, func, PCI_CLASS_REVISION);
                uint32_t class = class_rev >> 8;

                if (class == class_code) {
                    *out_bus = bus;
                    *out_dev = dev;
                    *out_func = func;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t* out_bus, uint8_t* out_dev, uint8_t* out_func) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_read_config(bus, dev, 0, PCI_VENDOR_ID) & 0xFFFF;

            if (vendor == 0xFFFF || vendor == 0x0000) {
                continue;
            }

            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vid = pci_read_config(bus, dev, func, PCI_VENDOR_ID) & 0xFFFF;
                uint16_t did = (pci_read_config(bus, dev, func, PCI_DEVICE_ID) >> 16) & 0xFFFF;

                if (vid == vendor_id && did == device_id) {
                    *out_bus = bus;
                    *out_dev = dev;
                    *out_func = func;
                    return 1;
                }
            }
        }
    }
    return 0;
}

uint32_t pci_get_bar(uint8_t bus, uint8_t dev, uint8_t func, int bar_num) {
    if (bar_num < 0 || bar_num > 5) return 0;
    return pci_read_config(bus, dev, func, PCI_BAR0 + (bar_num * 4));
}

void pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t cmd = pci_read_config(bus, dev, func, PCI_COMMAND) & 0xFFFF;
    cmd |= PCI_CMD_BUS_MASTER;
    cmd |= PCI_CMD_MEM_SPACE;
    cmd |= PCI_CMD_IO_SPACE;
    pci_write_config(bus, dev, func, PCI_COMMAND, cmd);
}

void pci_init(void) {

    uint16_t vendor = pci_read_config(0, 0, 0, PCI_VENDOR_ID) & 0xFFFF;

    if (vendor == 0xFFFF || vendor == 0x0000) {
        print("PCI: No PCI bus found\n");
        return;
    }

    int devices_found = 0;

    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_read_config(bus, dev, 0, PCI_VENDOR_ID) & 0xFFFF;

            if (vendor == 0xFFFF || vendor == 0x0000) {
                continue;
            }

            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vid = pci_read_config(bus, dev, func, PCI_VENDOR_ID) & 0xFFFF;

                if (vid == 0xFFFF || vid == 0x0000) {
                    if (func == 0) break;
                    continue;
                }

                uint32_t class_rev = pci_read_config(bus, dev, func, PCI_CLASS_REVISION);
                uint8_t class = (class_rev >> 24) & 0xFF;
                uint8_t subclass = (class_rev >> 16) & 0xFF;
                uint8_t prog_if = (class_rev >> 8) & 0xFF;

                devices_found++;
            }
        }
    }
}