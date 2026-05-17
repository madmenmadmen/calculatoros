#include "apic.h"
#include "acpi.h"
#include "io.h"

static volatile uint32_t* lapic = 0;

static uint32_t ioapic_base = 0;

extern void print(const char* str);
extern void print_hex(uint32_t num);

#define IOREGSEL 0x00
#define IOWIN    0x10

static void ioapic_write(uint32_t reg, uint32_t value) {

    volatile uint32_t* sel =
        (volatile uint32_t*)(ioapic_base + IOREGSEL);

    volatile uint32_t* win =
        (volatile uint32_t*)(ioapic_base + IOWIN);

    *sel = reg;
    *win = value;
}

static uint32_t ioapic_read(uint32_t reg) {

    volatile uint32_t* sel =
        (volatile uint32_t*)(ioapic_base + IOREGSEL);

    volatile uint32_t* win =
        (volatile uint32_t*)(ioapic_base + IOWIN);

    *sel = reg;

    return *win;
}

void apic_init(void) {

    if (!g_madt) {

        print("APIC: no MADT\n");

        return;
    }

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    uint32_t eax, edx;

    __asm__ volatile(
        "rdmsr"
        : "=a"(eax), "=d"(edx)
        : "c"(0x1B)
        );

    eax |= (1 << 11);

    __asm__ volatile(
        "wrmsr"
        :
    : "a"(eax), "d"(edx), "c"(0x1B)
        );

    uint32_t lapic_base = eax & 0xFFFFF000;

    lapic = (volatile uint32_t*)lapic_base;

    lapic[0xF0 / 4] = 0x1FF;

    lapic[0x80 / 4] = 0;

    print("LAPIC at 0x");
    print_hex(lapic_base);
    print("\n");

    uint8_t* ptr =
        (uint8_t*)g_madt + sizeof(struct madt);

    uint8_t* end =
        (uint8_t*)g_madt + g_madt->header.length;

    while (ptr < end) {

        uint8_t type = ptr[0];
        uint8_t len = ptr[1];

        if (type == 1) {

            ioapic_base =
                *(uint32_t*)(ptr + 4);

            break;
        }

        ptr += len;
    }

    if (!ioapic_base) {

        print("IOAPIC not found\n");

        return;
    }

    print("IOAPIC at 0x");
    print_hex(ioapic_base);
    print("\n");

    print("IOAPIC configured\n");
}

void apic_send_eoi(void) {

    if (lapic) {

        lapic[0xB0 / 4] = 0;
    }
}

void ioapic_set_irq(int irq, int vector) {
    uint32_t reg = 0x10 + irq * 2;

    uint32_t low =
        vector |
        (0 << 8) |
        (0 << 11);

    uint32_t high = 0;

    ioapic_write(reg, low);
    ioapic_write(reg + 1, high);
}