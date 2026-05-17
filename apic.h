#ifndef APIC_H
#define APIC_H

#include <stdint.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_ENABLE (1 << 11)

#define LAPIC_ID       0x20
#define LAPIC_VERSION  0x30
#define LAPIC_EOI      0xB0
#define LAPIC_SIVR     0xF0
#define LAPIC_TPR 0x80

#define IOAPIC_IOREGSEL  0x00
#define IOAPIC_IOWIN     0x10

void apic_init(void);
void apic_send_eoi(void);

#endif