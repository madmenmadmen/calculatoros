#pragma once
#include <stdint.h>

void keyboard_handler();
int has_key();
uint8_t get_key();
void flush_keys();