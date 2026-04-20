#include "idt.h"

idt_entry_t idt[256];
idt_ptr_t idtp;

extern void idt_load(uint32_t);
extern void irq0();
extern void irq1();

void idt_set_gate(int n, uint32_t handler) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x10; // а не 0x08
    idt[n].zero = 0;
    idt[n].type_attr = 0x8E; // interrupt gate
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_init() {
    idtp.limit = sizeof(idt_entry_t) * 256 - 1;
    idtp.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0);

    idt_set_gate(32, (uint32_t)irq0); // PIT
    idt_set_gate(33, (uint32_t)irq1); // keyboard

    idt_load((uint32_t)&idtp);
}