#pragma once
#include <stdint.h>

// Инициализация PIC (remap)
void pic_remap();

// Разрешить IRQ
void pic_enable_irq(uint8_t irq);

// Запретить IRQ
void pic_disable_irq(uint8_t irq);

// Отправка EOI (конец прерывания)
void pic_send_eoi(uint8_t irq);