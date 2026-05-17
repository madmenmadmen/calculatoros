#include "pit.h"
#include "io.h"
#include "pic.h"

#define PIT_PORT_COUNTER0 0x40
#define PIT_PORT_COMMAND  0x43

static volatile uint32_t ticks = 0;
extern void cursor_blink();

void init_pit() {
    uint32_t divisor = 1193182 / 60;

    outb(PIT_PORT_COMMAND, 0x34);
    outb(PIT_PORT_COUNTER0, divisor & 0xFF);
    outb(PIT_PORT_COUNTER0, (divisor >> 8) & 0xFF);
}

void pit_handler() {
    ticks++;

    static uint32_t last = 0;

    if (ticks - last >= 30) {
        last = ticks;
        cursor_blink();
    }
    pic_send_eoi(0);
}

uint32_t get_ticks() {
    return ticks;
}