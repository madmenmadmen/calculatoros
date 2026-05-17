#ifndef HPET_H
#define HPET_H

#include <stdint.h>

void hpet_init(void);
void hpet_handler(void);

uint64_t hpet_get_ticks(void);

#endif