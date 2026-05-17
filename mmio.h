#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

static inline uint32_t mmio_read(volatile void* addr) {
    return *(volatile uint32_t*)addr;
}

static inline void mmio_write(volatile void* addr, uint32_t val) {
    *(volatile uint32_t*)addr = val;
}

static inline uint16_t mmio_read16(volatile void* addr) {
    return *(volatile uint16_t*)addr;
}

static inline void mmio_write16(volatile void* addr, uint16_t val) {
    *(volatile uint16_t*)addr = val;
}

static inline uint8_t mmio_read8(volatile void* addr) {
    return *(volatile uint8_t*)addr;
}

static inline void mmio_write8(volatile void* addr, uint8_t val) {
    *(volatile uint8_t*)addr = val;
}

#endif