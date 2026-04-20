#include "keyboard_buffer.h"
#include "io.h"
#include "pic.h"

#define KEYBUF_SIZE 256

static volatile uint8_t keybuf[KEYBUF_SIZE];
static volatile int key_head = 0;
static volatile int key_tail = 0;

void keyboard_handler() {
    uint8_t scancode = inb(0x60);
    int next = (key_head + 1) % KEYBUF_SIZE;

    if (next != key_tail) {
        keybuf[key_head] = scancode;
        key_head = next;
    }
    pic_send_eoi(1);
}

int has_key() {
    return key_tail != key_head;
}

uint8_t get_key() {
    if (key_tail == key_head) return 0;
    uint8_t sc = keybuf[key_tail];
    key_tail = (key_tail + 1) % KEYBUF_SIZE;
    return sc;
}

void flush_keys() {
    key_tail = key_head;
}