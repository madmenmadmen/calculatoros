#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdint.h>

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_len;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_len;
    uint8_t  cb[16];
} __attribute__((packed)) cbw_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t  status;
} __attribute__((packed)) csw_t;

typedef struct {
    int present;
    int port;
    uint8_t address;
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint32_t lba_count;
    uint32_t block_size;
    uint8_t bulk_in_toggle;
    uint8_t bulk_out_toggle;
} usb_device_t;

#define CBW_SIGNATURE  0x43425355
#define CSW_SIGNATURE  0x53425355
#define SCSI_READ_CAPACITY 0x25
#define SCSI_READ_10       0x28
#define SCSI_WRITE_10      0x2A
#define CBW_SIZE 31
#define CSW_SIZE 13

void usb_msc_clear_endpoint_halt(uint8_t addr, uint8_t ep, int is_in,
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int));

int usb_msc_synchronize_cache(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int));

int usb_msc_read_capacity(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    uint32_t* sectors, uint32_t* block_size,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int));

int usb_msc_read_10(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    uint32_t lba, uint16_t count, void* buf,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int));

int usb_msc_write_10(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out,
    uint32_t lba, uint8_t count, const void* buf,
    int (*bulk_transfer)(int, uint8_t, uint8_t, uint8_t*, int, int, int),
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int));

void usb_msc_bot_reset(uint8_t addr, int interface,
    int (*control_transfer)(uint8_t, usb_setup_t*, void*, int));

#endif