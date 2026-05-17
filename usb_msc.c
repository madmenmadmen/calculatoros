#include "usb_msc.h"
#include <stddef.h>

extern void* alloc_phys(int pages);
extern void free_phys(void* addr, int pages);
extern void print(const char* str);
extern void print_hex(uint32_t val);

static void msc_udelay(int us) {
    for (volatile int i = 0; i < us * 10; i++);
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void usb_msc_clear_endpoint_halt(uint8_t addr, uint8_t ep, int is_in,
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int)) {
    usb_setup_t setup = {
        .bmRequestType = 0x02,
        .bRequest = 0x01,
        .wValue = 0,
        .wIndex = ep,
        .wLength = 0
    };
    control_transfer(addr, &setup, NULL, 0);
}

int usb_msc_synchronize_cache(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int)) {

    cbw_t* cbw = (cbw_t*)alloc_phys(1);
    csw_t* csw = (csw_t*)alloc_phys(1);

    if (!cbw || !csw) {
        if (cbw) free_phys(cbw, 1);
        if (csw) free_phys(csw, 1);
        return 0;
    }

    memset(cbw, 0, 4096);
    memset(csw, 0, 4096);

    cbw->signature = CBW_SIGNATURE;
    cbw->tag = 0x12345678;
    cbw->data_len = 0;
    cbw->flags = 0x00;
    cbw->lun = 0;
    cbw->cb_len = 10;
    cbw->cb[0] = 0x35;

    usb_msc_clear_endpoint_halt(addr, bulk_out, 0, control_transfer);
    usb_msc_clear_endpoint_halt(addr, bulk_in, 1, control_transfer);

    if (!bulk_transfer(idx, addr, bulk_out, (uint8_t*)cbw, 31, 0, 1))
        goto error;

    msc_udelay(100);

    if (!bulk_transfer(idx, addr, bulk_in, (uint8_t*)csw, CSW_SIZE, 1, 1))
        goto error;

    int ok = (csw->status == 0);
    free_phys(cbw, 1);
    free_phys(csw, 1);
    return ok;

error:
    free_phys(cbw, 1);
    free_phys(csw, 1);
    return 0;
}

int usb_msc_read_capacity(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    uint32_t* sectors, uint32_t* block_size,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int)) {

    cbw_t* cbw = (cbw_t*)alloc_phys(1);
    csw_t* csw = (csw_t*)alloc_phys(1);
    uint8_t* data = (uint8_t*)alloc_phys(1);

    if (!cbw || !csw || !data)
        goto error;

    memset(cbw, 0, 4096);
    memset(csw, 0, 4096);
    memset(data, 0, 4096);

    cbw->signature = CBW_SIGNATURE;
    cbw->tag = 0x12345678;
    cbw->data_len = 8;
    cbw->flags = 0x80;
    cbw->lun = 0;
    cbw->cb_len = 10;
    cbw->cb[0] = 0x25;

    usb_msc_clear_endpoint_halt(addr, bulk_out, 0, control_transfer);
    usb_msc_clear_endpoint_halt(addr, bulk_in, 1, control_transfer);

    if (!bulk_transfer(idx, addr, bulk_out, (uint8_t*)cbw, 31, 0, 1))
        goto error;

    msc_udelay(100);

    if (!bulk_transfer(idx, addr, bulk_in, data, 8, 1, 0))
        goto error;

    if (!bulk_transfer(idx, addr, bulk_in, (uint8_t*)csw, CSW_SIZE, 1, 1))
        goto error;

    if (csw->signature != CSW_SIGNATURE || csw->status != 0)
        goto error;

    *sectors = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    *block_size = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    (*sectors)++;

    free_phys(cbw, 1);
    free_phys(csw, 1);
    free_phys(data, 1);
    return 1;

error:
    if (cbw) free_phys(cbw, 1);
    if (csw) free_phys(csw, 1);
    if (data) free_phys(data, 1);
    return 0;
}

int usb_msc_read_10(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    uint32_t lba, uint16_t count, void* buf,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int)) {

    cbw_t* cbw = (cbw_t*)alloc_phys(1);
    csw_t* csw = (csw_t*)alloc_phys(1);

    if (!cbw || !csw)
        goto error;

    memset(cbw, 0, 4096);
    memset(csw, 0, 4096);

    cbw->signature = CBW_SIGNATURE;
    cbw->tag = 0x12345678;
    cbw->data_len = count * 512;
    cbw->flags = 0x80;
    cbw->lun = 0;
    cbw->cb_len = 10;

    cbw->cb[0] = 0x28;
    cbw->cb[2] = (lba >> 24) & 0xFF;
    cbw->cb[3] = (lba >> 16) & 0xFF;
    cbw->cb[4] = (lba >> 8) & 0xFF;
    cbw->cb[5] = lba & 0xFF;
    cbw->cb[7] = (count >> 8) & 0xFF;
    cbw->cb[8] = count & 0xFF;

    usb_msc_clear_endpoint_halt(addr, bulk_out, 0, control_transfer);
    usb_msc_clear_endpoint_halt(addr, bulk_in, 1, control_transfer);

    if (!bulk_transfer(idx, addr, bulk_out, (uint8_t*)cbw, 31, 0, 1))
        goto error;

    msc_udelay(100);

    if (!bulk_transfer(idx, addr, bulk_in, (uint8_t*)buf, count * 512, 1, 0))
        goto error;

    if (!bulk_transfer(idx, addr, bulk_in, (uint8_t*)csw, CSW_SIZE, 1, 1))
        goto error;

    if (csw->signature == CSW_SIGNATURE && csw->tag == cbw->tag && csw->status == 0) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return 1;
    }

error:
    if (cbw) free_phys(cbw, 1);
    if (csw) free_phys(csw, 1);
    return 0;
}

int usb_msc_write_10(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    uint32_t lba, uint8_t count, const void* buf,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int)) {

    cbw_t* cbw = (cbw_t*)alloc_phys(1);
    csw_t* csw = (csw_t*)alloc_phys(1);

    if (!cbw || !csw)
        goto error;

    memset(cbw, 0, 4096);
    memset(csw, 0, 4096);

    cbw->signature = CBW_SIGNATURE;
    cbw->tag = 0x12345678;
    cbw->data_len = count * 512;
    cbw->flags = 0x00;
    cbw->lun = 0;
    cbw->cb_len = 10;

    cbw->cb[0] = 0x2A;
    cbw->cb[2] = (lba >> 24) & 0xFF;
    cbw->cb[3] = (lba >> 16) & 0xFF;
    cbw->cb[4] = (lba >> 8) & 0xFF;
    cbw->cb[5] = lba & 0xFF;
    cbw->cb[7] = (count >> 8) & 0xFF;
    cbw->cb[8] = count & 0xFF;

    usb_msc_clear_endpoint_halt(addr, bulk_out, 0, control_transfer);
    usb_msc_clear_endpoint_halt(addr, bulk_in, 1, control_transfer);

    if (!bulk_transfer(idx, addr, bulk_out, (uint8_t*)cbw, 31, 0, 1))
        goto error;

    msc_udelay(100);

    if (!bulk_transfer(idx, addr, bulk_out, (uint8_t*)buf, count * 512, 0, 0))
        goto error;

    if (!bulk_transfer(idx, addr, bulk_in, (uint8_t*)csw, CSW_SIZE, 1, 1))
        goto error;

    if (csw->signature == CSW_SIGNATURE && csw->tag == cbw->tag && csw->status == 0) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return usb_msc_synchronize_cache(idx, addr, bulk_in, bulk_out, bulk_transfer, control_transfer);
    }

error:
    if (cbw) free_phys(cbw, 1);
    if (csw) free_phys(csw, 1);
    return 0;
}

void usb_msc_bot_reset(uint8_t addr, int interface,
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int)) {
    usb_setup_t setup = {
        .bmRequestType = 0x21,
        .bRequest = 0xFF,
        .wValue = 0,
        .wIndex = interface,
        .wLength = 0
    };
    control_transfer(addr, &setup, NULL, 0);
}