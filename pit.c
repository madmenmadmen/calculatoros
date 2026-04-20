#include "pit.h"
#include "io.h"
#include "pic.h"

#define PIT_PORT_COUNTER0 0x40
#define PIT_PORT_COMMAND  0x43

static volatile uint32_t ticks = 0;
static volatile int pit_cursor_counter = 0;
static volatile int cursor_blink_state = 1;  // 1 = видимый, 0 = скрытый

extern void hide_cursor();
extern void show_cursor();

void init_pit() {
    uint32_t divisor = 1193182 / 60;  // 60 Гц

    outb(PIT_PORT_COMMAND, 0x34);
    outb(PIT_PORT_COUNTER0, divisor & 0xFF);
    outb(PIT_PORT_COUNTER0, (divisor >> 8) & 0xFF);
}

void pit_handler() {
    ticks++;

    // Мигание курсора: меняем состояние каждые 30 тиков (0.5 секунды при 60Гц)
    pit_cursor_counter++;
    if (pit_cursor_counter >= 30) {
        pit_cursor_counter = 0;
        cursor_blink_state = !cursor_blink_state;

        if (cursor_blink_state) {
            show_cursor();
        }
        else {
            hide_cursor();
        }
    }

    pic_send_eoi(0);
}

uint32_t get_ticks() {
    return ticks;
}

int is_cursor_visible() {
    return cursor_blink_state;
}