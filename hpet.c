#include "hpet.h"
#include "acpi.h"
#include "apic.h"
#include "io.h"

#define HPET_ENABLE_CNF     (1 << 0)

static volatile uint64_t* hpet = 0;
static volatile uint64_t ticks = 0;

static uint64_t interval = 0;
static uint64_t last = 0;

extern void update(void);
extern void cursor_blink(void);
extern void update_cube(void);
extern void draw_mouse_cursor(void);
extern void flip_buffer(void);
extern void vesa_draw_cursor(void);

extern int cube_enabled;
extern int cursor_visible;

static inline uint64_t hpet_read(uint32_t reg) {
    return hpet[reg / 8];
}

static inline void hpet_write(uint32_t reg, uint64_t value) {
    hpet[reg / 8] = value;
}

void hpet_init(void) {

    if (!g_hpet) {
        print("HPET not found\n");
        return;
    }

    hpet = (uint64_t*)(uint64_t)g_hpet->event_timer_block.address;

    hpet_write(0x10, 0);
    hpet_write(0xF0, 0);

    uint64_t caps = hpet_read(0x0);

    uint32_t freq = (uint32_t)(caps >> 32);

    interval = 14000000 / 60;

    uint64_t cfg = hpet_read(0x100);

    cfg &= ~(1ULL << 3);
    cfg |= (1ULL << 2);

    cfg &= ~(0x1FULL << 9);
    cfg |= (2ULL << 9);

    hpet_write(0x100, cfg);

    hpet_write(0x108, interval);
    hpet_write(0x10, 1);
}

void hpet_handler(void) {

    ticks++;

    update();

    static uint64_t blink = 0;

    if ((ticks - blink) >= 60) {
        blink = ticks;
        cursor_blink();
    }

    if (cube_enabled) update_cube();
    if (cursor_visible) vesa_draw_cursor();

    draw_mouse_cursor();

    flip_buffer();

    console_func();

    hpet_write(0x108, interval);

    apic_send_eoi();
}