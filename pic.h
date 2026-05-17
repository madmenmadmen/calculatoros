#pragma once
#include <stdint.h>

void pic_remap();

void pic_enable_irq(uint8_t irq);

void pic_disable_irq(uint8_t irq);

void pic_send_eoi(uint8_t irq);