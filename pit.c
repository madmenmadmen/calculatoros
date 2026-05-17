#include "pit.h"
#include "io.h"
#include "pic.h"

#define PIT_PORT_COUNTER0 0x40
#define PIT_PORT_COMMAND  0x43

static volatile uint32_t ticks = 0;
extern void cursor_blink();
extern void update_cube();
extern void cube_restore_background();
extern int cube_enabled;
extern void update();
extern int cursor_visible;
extern void draw_mouse_cursor();

void init_pit() {
    uint32_t divisor = 1193182 / 60;

    outb(PIT_PORT_COMMAND, 0x34);
    outb(PIT_PORT_COUNTER0, divisor & 0xFF);
    outb(PIT_PORT_COUNTER0, (divisor >> 8) & 0xFF);
}

void pit_handler() {
    ticks++;

    update();

    static uint32_t last = 0;
    static int cube_tick = 0;

    if (ticks - last >= 30) {
        last = ticks;
        cursor_blink();
    }

    if (cube_enabled) {
        update_cube();
    }

    if (cursor_visible) {
        vesa_draw_cursor();
    }

    draw_mouse_cursor();

    flip_buffer();

    pic_send_eoi(0);
}

uint32_t get_ticks() {
    return ticks;
}