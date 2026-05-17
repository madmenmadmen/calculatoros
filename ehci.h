#ifndef EHCI_H
#define EHCI_H

#include <stdint.h>

#define EHCI_PCI_CLASS      0x0C
#define EHCI_PCI_SUBCLASS   0x03
#define EHCI_PCI_PROG_IF    0x20
#define EHCI_PCI_CLASS_CODE 0x0C0320

#define EHCI_CAPLENGTH      0x00
#define EHCI_HCIVERSION     0x02
#define EHCI_HCSPARAMS      0x04
#define EHCI_HCCPARAMS      0x08
#define EHCI_HCSP_PORTROUTE 0x0C

#define HCSP_N_PORTS_MASK   0x0F
#define HCSP_N_PCC_MASK     0xF00
#define HCSP_N_CC_MASK      0xF000
#define HCSP_PPC            (1 << 4)
#define HCSP_P_INDICATOR    (1 << 16)

#define HCC_64BIT           (1 << 0)
#define HCC_PFLF            (1 << 1)
#define HCC_ASPC            (1 << 2)
#define HCC_EECP_MASK       0xFF00

#define EHCI_USBCMD         0x00
#define EHCI_USBSTS         0x04
#define EHCI_USBINTR        0x08
#define EHCI_FRINDEX        0x0C
#define EHCI_CTRLDSSEGMENT  0x10
#define EHCI_PERIODICLISTBASE 0x14
#define EHCI_ASYNCLISTADDR  0x18
#define EHCI_CONFIGFLAG     0x40
#define EHCI_PORTSC_BASE    0x44

#define USBCMD_RS           (1 << 0)
#define USBCMD_HCRESET      (1 << 1)
#define USBCMD_FLS_MASK     (3 << 2)
#define USBCMD_FLS_1024     (0 << 2)
#define USBCMD_FLS_512      (1 << 2)
#define USBCMD_FLS_256      (2 << 2)
#define USBCMD_PSE          (1 << 4)
#define USBCMD_ASE          (1 << 5)
#define USBCMD_IAA          (1 << 6)
#define USBCMD_ITC_MASK     (0xFF << 16)

#define USBSTS_USBINT       (1 << 0)
#define USBSTS_USBERRINT    (1 << 1)
#define USBSTS_PORTCHANGE   (1 << 2)
#define USBSTS_FATAL        (1 << 4)
#define USBSTS_HALTED       (1 << 12)
#define USBSTS_RECLAIM      (1 << 13)
#define USBSTS_ASS          (1 << 14)
#define USBSTS_PSS          (1 << 15)

#define USBINTR_UE   (1 << 0)
#define USBINTR_UEE  (1 << 1)
#define USBINTR_PCE  (1 << 2)
#define USBINTR_FRE  (1 << 3)
#define USBINTR_AAE  (1 << 5)

#define PORTSC_CCS          (1 << 0)
#define PORTSC_CSC          (1 << 1)
#define PORTSC_PE           (1 << 2)
#define PORTSC_PEC          (1 << 3)
#define PORTSC_OCA          (1 << 4)
#define PORTSC_OCC          (1 << 5)
#define PORTSC_FPR          (1 << 6)
#define PORTSC_SUSPEND      (1 << 7)
#define PORTSC_PR           (1 << 8)
#define PORTSC_PP           (1 << 12)
#define PORTSC_LS_MASK      (3 << 10)
#define PORTSC_LS_J         (2 << 10)
#define PORTSC_LS_K         (1 << 10)
#define PORTSC_LS_SE0       (0 << 10)
#define PORTSC_PO           (1 << 13)
#define PORTSC_PIC_MASK     (3 << 14)

typedef struct {
    uint32_t next;
    uint32_t alt_next;
    uint32_t token;
    uint32_t buffer[5];
} __attribute__((packed, aligned(32))) ehci_qtd_t;

typedef struct {
    uint32_t horiz_link_ptr;

    uint32_t ep_char;
    uint32_t ep_caps;

    uint32_t current_qtd;

    uint32_t next_qtd;
    uint32_t alt_next_qtd;

    uint32_t token;

    uint32_t buffer[5];

    uint32_t ext_buffer[5];

} __attribute__((packed, aligned(32))) ehci_qh_t;

#define QTD_ACTIVE      (1 << 7)
#define QTD_HALTED      (1 << 6)
#define QTD_BUFFER_ERR  (1 << 5)
#define QTD_BABBLE      (1 << 4)
#define QTD_XACT_ERR    (1 << 3)
#define QTD_MISSED      (1 << 2)

#define PID_OUT         0x00
#define PID_IN          0x01
#define PID_SETUP       0x02

typedef struct {
    uint32_t ptr : 5;
    uint32_t link_ptr : 27;
    uint32_t typ : 2;
    uint32_t t : 1;
} __attribute__((packed)) ehci_framelist_entry_t;

#define FRAMELIST_TYP_QH 0x01

void ehci_init(void);
int ehci_get_port_count(void);
int ehci_port_has_device(int port);
int ehci_port_reset(int port);
int ehci_bulk_transfer(int port, uint8_t ep, uint8_t* data, int len, int is_in);
int ehci_usb_read(int idx, uint32_t lba, uint8_t count, void* buf);
int ehci_usb_get_count(void);
void ehci_usb_get_info(int idx, uint32_t* sectors, uint32_t* block_size);
void ehci_irq_handler(void);
void ehci_init_device_on_port(int port);
void ehci_remove_device(int port);
void ehci_poll_ports(void);

#endif