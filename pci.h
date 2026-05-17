#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_SUBCLASS_SATA       0x06
#define PI_AHCI                 0x01

#define PCI_CLASS_UHCI          0x0C0300
#define PCI_CLASS_EHCI          0x0C0320
#define PCI_CLASS_OHCI          0x0C0310
#define PCI_CLASS_AHCI          0x010601

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_CLASS_REVISION  0x08
#define PCI_CLASS_CODE      0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS_PROG_IF   0x09
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D

#define PCI_CMD_IO_SPACE     (1 << 0)
#define PCI_CMD_MEM_SPACE    (1 << 1)
#define PCI_CMD_BUS_MASTER   (1 << 2)
#define PCI_CMD_INT_DISABLE  (1 << 10)

uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
int pci_find_class(uint32_t class_code, uint8_t* out_bus, uint8_t* out_dev, uint8_t* out_func);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t* out_bus, uint8_t* out_dev, uint8_t* out_func);
uint32_t pci_get_bar(uint8_t bus, uint8_t dev, uint8_t func, int bar_num);
void pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t func);
void pci_init(void);

#endif