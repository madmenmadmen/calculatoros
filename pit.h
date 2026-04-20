#pragma once
#include <stdint.h>

void init_pit();
void pit_handler();
uint32_t get_ticks();
int is_cursor_visible();