#include "ehci.h"
#include "pci.h"
#include "mmio.h"
#include "io.h"
#include <stddef.h>

#define MAX_PORTS 8
#define FRAME_LIST_SIZE 1024
#define MAX_QH 32
#define MAX_DEVICES 4

#define USB_REQ_GET_DESCRIPTOR   0x06
#define USB_REQ_SET_ADDRESS      0x05
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_DESC_DEVICE          0x01
#define USB_DESC_CONFIGURATION   0x02
#define USB_DIR_IN               0x80
#define USB_DIR_OUT              0x00
#define USB_TYPE_STANDARD        0x00
#define USB_RECIP_DEVICE         0x00

#define CBW_SIGNATURE  0x43425355
#define CSW_SIGNATURE  0x53425355
#define SCSI_READ_CAPACITY 0x25
#define SCSI_READ_10       0x28
#define SCSI_WRITE_10      0x2A
#define SCSI_INQUIRY       0x12

static int port_initialized[MAX_PORTS];
static uint32_t last_portsc[MAX_PORTS] = { 0 };

static void* memcpy(void* dest, const void* src, int n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

static uintptr_t ehci_bar = 0;
static int ehci_port_count = 0;
static uint32_t* frame_list = NULL;
static ehci_qh_t* async_qh = NULL;
static int qh_initialized = 0;

static usb_device_t usb_devices[MAX_DEVICES];
static int usb_device_count = 0;

extern void* alloc_phys(int pages);
extern void free_phys(void* addr, int pages);

static volatile uint32_t* ehci_cap(uint32_t offset) {
    return (volatile uint32_t*)(ehci_bar + offset);
}

static volatile uint32_t* ehci_op(uint32_t offset) {
    uint8_t caplen = mmio_read8(ehci_cap(EHCI_CAPLENGTH));
    return (volatile uint32_t*)(ehci_bar + caplen + offset);
}

static volatile uint32_t* ehci_portsc(int port) {
    return ehci_op(EHCI_PORTSC_BASE + (port * 4));
}

static void ehci_udelay(int us) {
    for (volatile int i = 0; i < us * 10; i++);
}

static void ehci_mdelay(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++);
}

static int ehci_find_controller(void) {
    uint8_t bus, dev, func;

    if (!pci_find_class(EHCI_PCI_CLASS_CODE, &bus, &dev, &func)) {
        return 0;
    }

    uint32_t bar0 = pci_get_bar(bus, dev, func, 0);
    ehci_bar = bar0 & ~0xF;

    pci_enable_bus_master(bus, dev, func);

    return 1;
}

static int ehci_reset_controller(void) {
    volatile uint32_t* usbcmd = ehci_op(EHCI_USBCMD);
    volatile uint32_t* usbsts = ehci_op(EHCI_USBSTS);

    mmio_write(usbcmd, USBCMD_HCRESET);
    ehci_mdelay(10);

    for (int i = 0; i < 100; i++) {
        if (!(mmio_read(usbcmd) & USBCMD_HCRESET))
            break;
        ehci_mdelay(1);
    }

    for (int i = 0; i < 100; i++) {
        if (mmio_read(usbsts) & USBSTS_HALTED)
            break;
        ehci_mdelay(1);
    }

    return 1;
}

static int ehci_init_frame_list(void) {
    frame_list = (uint32_t*)alloc_phys(1);
    if (!frame_list) return 0;

    memset((void*)frame_list, 0, 4096);
    mmio_write(ehci_op(EHCI_PERIODICLISTBASE), (uint32_t)frame_list);

    return 1;
}

static ehci_qtd_t* ehci_create_qtd(uint32_t buffer, uint32_t len, uint8_t pid, int toggle, int ioc) {
    ehci_qtd_t* qtd = (ehci_qtd_t*)alloc_phys(1);
    if (!qtd) return NULL;
    memset((void*)qtd, 0, 4096);

    qtd->next = 1;
    qtd->alt_next = 1;

    uint32_t token = QTD_ACTIVE |
        ((len & 0x7FFF) << 16) |
        ((toggle & 1) << 31) |
        (pid << 8);

    if (ioc) {
        token |= (1 << 15);
    }

    qtd->token = token;

    uint32_t page = buffer & ~0xFFF;
    qtd->buffer[0] = buffer;
    for (int i = 1; i < 5; i++) {
        page += 0x1000;
        qtd->buffer[i] = page;
    }

    return qtd;
}

static int ehci_control_transfer(
    uint8_t dev_addr,
    usb_setup_t* setup,
    void* data,
    int data_len
) {
    ehci_qh_t* qh = alloc_phys(1);
    if (!qh) return 0;

    memset(qh, 0, 4096);

    ehci_qtd_t* setup_qtd =
        ehci_create_qtd(
            (uint32_t)setup,
            sizeof(usb_setup_t),
            PID_SETUP,
            0,
            0
        );

    if (!setup_qtd) {
        free_phys(qh, 1);
        return 0;
    }

    ehci_qtd_t* data_qtd = NULL;

    if (data && data_len > 0) {
        data_qtd = ehci_create_qtd(
            (uint32_t)data,
            data_len,
            (setup->bmRequestType & USB_DIR_IN)
            ? PID_IN
            : PID_OUT,
            1,
            0
        );

        if (!data_qtd) {
            free_phys(setup_qtd, 1);
            free_phys(qh, 1);
            return 0;
        }
    }

    ehci_qtd_t* status_qtd =
        ehci_create_qtd(
            0,
            0,
            (setup->bmRequestType & USB_DIR_IN)
            ? PID_OUT
            : PID_IN,
            1,
            0
        );

    if (!status_qtd) {
        free_phys(setup_qtd, 1);

        if (data_qtd)
            free_phys(data_qtd, 1);

        free_phys(qh, 1);
        return 0;
    }

    setup_qtd->next =
        data_qtd
        ? (uint32_t)data_qtd
        : (uint32_t)status_qtd;

    if (data_qtd)
        data_qtd->next =
        (uint32_t)status_qtd;

    qh->horiz_link_ptr =
        ((uint32_t)async_qh & ~0x1F) |
        (1 << 1);

    qh->ep_char =
        (64 << 16) |
        (2 << 12) |
        dev_addr;

    qh->current_qtd = 0;

    qh->next_qtd =
        (uint32_t)setup_qtd;

    qh->alt_next_qtd = 1;

    uint32_t old = async_qh->horiz_link_ptr;

    qh->horiz_link_ptr = old;

    MEMORY_BARRIER();

    async_qh->horiz_link_ptr =
        ((uint32_t)qh & ~0x1F) | (1 << 1);

    MEMORY_BARRIER();

    for (int i = 0; i < 100000; i++) {

        if (!(status_qtd->token & QTD_ACTIVE))
            break;

        ehci_udelay(10);
    }

    READ_BARRIER();

    int ok =
        !(status_qtd->token & QTD_HALTED);

    async_qh->horiz_link_ptr = old;

    free_phys(setup_qtd, 1);

    if (data_qtd)
        free_phys(data_qtd, 1);

    free_phys(status_qtd, 1);
    free_phys(qh, 1);

    return ok;
}

static int ehci_init_async_qh(void) {
    async_qh = (ehci_qh_t*)alloc_phys(1);
    if (!async_qh) return 0;

    memset(async_qh, 0, 4096);

    async_qh->horiz_link_ptr =
        ((uint32_t)async_qh & ~0x1F) |
        (1 << 1);

    async_qh->ep_char =
        (1 << 15) |
        (2 << 12) |
        (64 << 16);

    async_qh->next_qtd = 1;
    async_qh->alt_next_qtd = 1;

    mmio_write(
        ehci_op(EHCI_ASYNCLISTADDR),
        (uint32_t)async_qh
    );

    return 1;
}

static void ehci_configure_ports(void) {
    mmio_write(ehci_op(EHCI_CONFIGFLAG), 1);
}

static void ehci_run_controller(void) {
    volatile uint32_t* usbcmd = ehci_op(EHCI_USBCMD);
    volatile uint32_t* usbsts = ehci_op(EHCI_USBSTS);

    uint32_t cmd = mmio_read(usbcmd);
    cmd |= USBCMD_RS;
    mmio_write(usbcmd, cmd);

    for (int i = 0; i < 100; i++) {
        if (!(mmio_read(usbsts) & USBSTS_HALTED))
            break;
        ehci_udelay(100);
    }
}

static void ehci_enable_async_schedule(void) {
    volatile uint32_t* usbcmd = ehci_op(EHCI_USBCMD);
    volatile uint32_t* usbsts = ehci_op(EHCI_USBSTS);

    uint32_t cmd = mmio_read(usbcmd);
    cmd |= USBCMD_ASE;
    mmio_write(usbcmd, cmd);

    for (int i = 0; i < 100; i++) {
        if (mmio_read(usbsts) & USBSTS_ASS)
            break;
        ehci_udelay(100);
    }
}

int ehci_port_reset(int port) {
    volatile uint32_t* portsc = ehci_portsc(port);

    uint32_t status = mmio_read(portsc);

    if (!(status & PORTSC_CCS)) {
        return 0;
    }

    uint32_t val = status;
    val &= ~PORTSC_PE;
    val |= PORTSC_PR;
    mmio_write(portsc, val);

    ehci_mdelay(10);

    val = mmio_read(portsc);
    val &= ~PORTSC_PR;
    mmio_write(portsc, val);

    ehci_mdelay(2);

    status = mmio_read(portsc);

    if (status & PORTSC_PE) {
        return 1;
    }
    else {
        val = status;
        val |= PORTSC_PO;
        mmio_write(portsc, val);
        return 0;
    }
}

static int ehci_bulk_transfer_dev(
    int idx,
    uint8_t dev_addr,
    uint8_t ep,
    uint8_t* data,
    int len,
    int is_in,
    int ioc
) {
    ehci_qh_t* qh = alloc_phys(1);
    if (!qh) return 0;
    memset(qh, 0, 4096);
    MEMORY_BARRIER();

    int toggle;
    if (is_in) {
        toggle = usb_devices[idx].bulk_in_toggle;
    }
    else {
        toggle = usb_devices[idx].bulk_out_toggle;
    }

    ehci_qtd_t* qtd = ehci_create_qtd((uint32_t)data, len,
        is_in ? PID_IN : PID_OUT, toggle, ioc);
    if (!qtd) {
        free_phys(qh, 1);
        return 0;
    }
    MEMORY_BARRIER();

    qh->horiz_link_ptr = ((uint32_t)async_qh & ~0x1F) | (1 << 1);
    qh->ep_char =
        ((dev_addr & 0x7F)) |
        ((ep & 0x0F) << 8) |
        (2 << 12) |
        (64 << 16) |
        (1 << 14);
    qh->current_qtd = 0;
    qh->next_qtd = (uint32_t)qtd;
    qh->alt_next_qtd = 1;

    uint32_t old = async_qh->horiz_link_ptr;
    async_qh->horiz_link_ptr = ((uint32_t)qh & ~0x1F) | (1 << 1);
    MEMORY_BARRIER();

    for (int i = 0; i < 100000; i++) {
        if (!(qtd->token & QTD_ACTIVE)) break;
        ehci_udelay(10);
    }
    READ_BARRIER();

    int ok = !(qtd->token & QTD_HALTED);
    async_qh->horiz_link_ptr = old;
    MEMORY_BARRIER();

    if (ok) {
        if (is_in) {
            usb_devices[idx].bulk_in_toggle ^= 1;
        }
        else {
            usb_devices[idx].bulk_out_toggle ^= 1;
        }
    }

    free_phys(qtd, 1);
    free_phys(qh, 1);
    return ok;
}

static void usb_clear_endpoint_halt(uint8_t addr, uint8_t ep, int is_in) {
    usb_setup_t setup = {
        .bmRequestType = (is_in ? 0x82 : 0x02),
        .bRequest = 0x01,
        .wValue = 0,
        .wIndex = ep,
        .wLength = 0
    };
    ehci_control_transfer(addr, &setup, NULL, 0);
}

static int usb_msc_synchronize_cache(
    int idx,
    uint8_t addr,
    uint8_t bulk_in,
    uint8_t bulk_out
) {
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
    cbw->cb[1] = 0;
    cbw->cb[2] = 0;
    cbw->cb[3] = 0;
    cbw->cb[4] = 0;
    cbw->cb[5] = 0;
    cbw->cb[6] = 0;
    cbw->cb[7] = 0;
    cbw->cb[8] = 0;
    cbw->cb[9] = 0;

    usb_clear_endpoint_halt(addr, bulk_out, 0);
    usb_clear_endpoint_halt(addr, bulk_in, 1);

    if (!ehci_bulk_transfer_dev(idx, addr, bulk_out, (uint8_t*)cbw, 31, 0, 1))
        return 0;

    ehci_udelay(100);

    if (!ehci_bulk_transfer_dev(idx, addr, bulk_in, (uint8_t*)csw, CSW_SIZE, 1, 1))
        return 0;

    int ok = (csw->status == 0);

    free_phys(cbw, 1);
    free_phys(csw, 1);

    return ok;
}

static int ehci_get_device_descriptor(uint8_t addr, uint8_t* buf) {
    void* phys_buf = alloc_phys(1);
    if (!phys_buf) return 0;

    usb_setup_t setup = {
        .bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_DEVICE << 8),
        .wIndex = 0,
        .wLength = 18
    };

    int result = ehci_control_transfer(addr, &setup, phys_buf, 18);

    if (result && buf) {
        memcpy(buf, phys_buf, 18);
    }

    free_phys(phys_buf, 1);
    return result;
}

static int ehci_set_address(uint8_t old_addr, uint8_t new_addr) {
    usb_setup_t setup = {
        .bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQ_SET_ADDRESS,
        .wValue = new_addr,
        .wIndex = 0,
        .wLength = 0
    };
    return ehci_control_transfer(old_addr, &setup, NULL, 0);
}

static int ehci_set_configuration(uint8_t addr, uint8_t config) {
    usb_setup_t setup = {
        .bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQ_SET_CONFIGURATION,
        .wValue = config,
        .wIndex = 0,
        .wLength = 0
    };
    return ehci_control_transfer(addr, &setup, NULL, 0);
}

static int ehci_get_config_descriptor(uint8_t addr, uint8_t* buf, int len) {

    void* phys_buf = alloc_phys((len + 4095) / 4096);
    if (!phys_buf) return 0;

    usb_setup_t setup = {
        .bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_CONFIGURATION << 8),
        .wIndex = 0,
        .wLength = len
    };

    int result = ehci_control_transfer(addr, &setup, phys_buf, len);

    if (result && buf) {

        memcpy(buf, phys_buf, len);
    }

    free_phys(phys_buf, (len + 4095) / 4096);
    return result;
}

static int ehci_parse_config(uint8_t* conf, int len, uint8_t* bulk_in, uint8_t* bulk_out) {
    int off = 0;
    *bulk_in = 0;
    *bulk_out = 0;

    while (off < len) {
        uint8_t desc_len = conf[off];
        uint8_t desc_type = conf[off + 1];
        if (desc_len == 0 || off + desc_len > len) break;

        if (desc_type == 0x05) {
            uint8_t addr = conf[off + 2];
            uint8_t attr = conf[off + 3];
            if ((attr & 0x03) == 0x02) {
                if (addr & 0x80) *bulk_in = addr;
                else *bulk_out = addr;
            }
        }
        off += desc_len;
    }

    return (*bulk_in && *bulk_out);
}

static int usb_msc_read_capacity(int idx, uint8_t addr, uint8_t bulk_in, uint8_t bulk_out, uint32_t* sectors, uint32_t* block_size) {
    cbw_t* cbw = alloc_phys(1);
    csw_t* csw = alloc_phys(1);
    uint8_t* data = alloc_phys(1);

    if (!cbw || !csw || !data)
        return 0;

    memset(cbw, 0, 4096);
    memset(csw, 0, 4096);
    memset(data, 0, 4096);

    cbw->signature = CBW_SIGNATURE;
    cbw->tag = 0x12345678;
    cbw->data_len = 8;
    cbw->flags = 0x80;
    cbw->lun = 0;
    cbw->cb_len = 10;
    cbw->cb[0] = SCSI_READ_CAPACITY;

    MEMORY_BARRIER();

    usb_clear_endpoint_halt(addr, bulk_out, 0);
    usb_clear_endpoint_halt(addr, bulk_in, 1);

    if (!ehci_bulk_transfer_dev(idx, addr, bulk_out,
        (uint8_t*)cbw, 31, 0, 1))
        return 0;

    ehci_udelay(100);

    if (!ehci_bulk_transfer_dev(idx, addr, bulk_in,
        data, 8, 1, 0))
        return 0;

    if (!ehci_bulk_transfer_dev(idx, addr, bulk_in,
        (uint8_t*)csw, CSW_SIZE, 1, 1))
        return 0;

    if (csw->status != 0)
        return 0;

    *sectors =
        (data[0] << 24) |
        (data[1] << 16) |
        (data[2] << 8) |
        data[3];

    *block_size =
        (data[4] << 24) |
        (data[5] << 16) |
        (data[6] << 8) |
        data[7];

    (*sectors)++;

    free_phys(cbw, 1);
    free_phys(csw, 1);
    free_phys(data, 1);

    return 1;
}

static int usb_msc_read_10(
    int idx,
    uint8_t addr,
    uint8_t bulk_in,
    uint8_t bulk_out,
    uint32_t lba,
    uint16_t count,
    void* buf
) {
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
    cbw->data_len = count * usb_devices[idx].block_size;
    cbw->flags = 0x80;
    cbw->lun = 0;
    cbw->cb_len = 10;

    cbw->cb[0] = SCSI_READ_10;
    cbw->cb[2] = (lba >> 24) & 0xFF;
    cbw->cb[3] = (lba >> 16) & 0xFF;
    cbw->cb[4] = (lba >> 8) & 0xFF;
    cbw->cb[5] = lba & 0xFF;
    cbw->cb[7] = (count >> 8) & 0xFF;
    cbw->cb[8] = count & 0xFF;

    MEMORY_BARRIER();

    if (cbw->signature != CBW_SIGNATURE) {
        print("ERROR: CBW signature mismatch! Expected 0x43425355, got ");
        print_hex(cbw->signature);
        print("\n");
    }

    if (cbw->cb_len != 10) {
        print("ERROR: cb_len = ");
        print_hex(cbw->cb_len);
        print(" (should be 10 for READ_CAPACITY)\n");
    }

    usb_clear_endpoint_halt(addr, bulk_out, 0);
    usb_clear_endpoint_halt(addr, bulk_in, 1);

    if (!ehci_bulk_transfer_dev(
        idx,
        addr,
        bulk_out,
        (uint8_t*)cbw,
        31,
        0,
        1
    )) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return 0;
    }

    ehci_udelay(100);

    if (!ehci_bulk_transfer_dev(
        idx,
        addr,
        bulk_in,
        (uint8_t*)buf,
        count * 512,
        1,
        0
    )) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return 0;
    }

    if (!ehci_bulk_transfer_dev(
        idx,
        addr,
        bulk_in,
        (uint8_t*)csw,
        CSW_SIZE,
        1,
        1
    )) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return 0;
    }

    MEMORY_BARRIER();

    int ok =
        (csw->signature == CSW_SIGNATURE) &&
        (csw->tag == cbw->tag) &&
        (csw->status == 0);

    free_phys(cbw, 1);
    free_phys(csw, 1);

    return ok;
}

static void usb_bot_reset(uint8_t addr, int interface) {
    usb_setup_t setup = {
        .bmRequestType = 0x21,
        .bRequest = 0xFF,
        .wValue = 0,
        .wIndex = interface,
        .wLength = 0
    };
    ehci_control_transfer(addr, &setup, NULL, 0);
}

static int ehci_enumerate_port(int port) {
    uint8_t dev_desc[18];
    uint8_t conf_buf[256];
    uint8_t bulk_in, bulk_out;
    uint32_t sectors, block_size;

    if (!ehci_get_device_descriptor(0, dev_desc)) {
        print("EHCI: GET_DESCRIPTOR failed\n");
        return 0;
    }

    uint8_t dev_addr = 1 + port;
    if (!ehci_set_address(0, dev_addr)) {
        print("EHCI: SET_ADDRESS failed\n");
        return 0;
    }

    ehci_mdelay(10);

    if (!ehci_get_device_descriptor(dev_addr, dev_desc)) {
        print("EHCI: Second GET_DESCRIPTOR failed\n");
        return 0;
    }

    uint8_t config_header[9];
    if (!ehci_get_config_descriptor(dev_addr, config_header, 9)) {
        print("EHCI: GET_CONFIG_DESC(9) failed\n");
        return 0;
    }

    int total_len = config_header[2] | (config_header[3] << 8);

    if (total_len == 0 || total_len > 256) {
        print("EHCI: Invalid config length\n");
        return 0;
    }

    if (!ehci_get_config_descriptor(dev_addr, conf_buf, total_len)) {
        print("EHCI: GET_FULL_CONFIG failed\n");
        return 0;
    }

    if (!ehci_parse_config(conf_buf, total_len, &bulk_in, &bulk_out)) {
        print("EHCI: No bulk endpoints (not Mass Storage)\n");
        return 0;
    }

    if (!ehci_set_configuration(dev_addr, conf_buf[5])) {
        print("EHCI: SET_CONFIGURATION failed\n");
        return 0;
    }

    usb_bot_reset(dev_addr, 0);
    ehci_mdelay(50);

    int idx = usb_device_count;
    if (!usb_msc_read_capacity(idx, dev_addr, bulk_in, bulk_out, &sectors, &block_size)) {
        print("EHCI: READ_CAPACITY failed\n");
        return 0;
    }

    if (usb_device_count < MAX_DEVICES) {
        usb_devices[usb_device_count].present = 1;
        usb_devices[usb_device_count].port = port;
        usb_devices[usb_device_count].address = dev_addr;
        usb_devices[usb_device_count].bulk_in_ep = bulk_in;
        usb_devices[usb_device_count].bulk_out_ep = bulk_out;
        usb_devices[usb_device_count].lba_count = sectors;
        usb_devices[usb_device_count].block_size = block_size;
        usb_devices[usb_device_count].bulk_in_toggle = 0;
        usb_devices[usb_device_count].bulk_out_toggle = 0;
        usb_device_count++;
    }

    return 1;
}

static int usb_msc_write_10(
    int idx,
    uint8_t addr,
    uint8_t bulk_in,
    uint8_t bulk_out,
    uint32_t lba,
    uint8_t count,
    const void* buf
) {
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
    cbw->data_len = count * usb_devices[idx].block_size;
    cbw->flags = 0x00;
    cbw->lun = 0;
    cbw->cb_len = 10;

    cbw->cb[0] = SCSI_WRITE_10;
    cbw->cb[2] = (lba >> 24) & 0xFF;
    cbw->cb[3] = (lba >> 16) & 0xFF;
    cbw->cb[4] = (lba >> 8) & 0xFF;
    cbw->cb[5] = lba & 0xFF;
    cbw->cb[7] = (count >> 8) & 0xFF;
    cbw->cb[8] = count & 0xFF;

    usb_clear_endpoint_halt(addr, bulk_out, 0);
    usb_clear_endpoint_halt(addr, bulk_in, 1);

    if (!ehci_bulk_transfer_dev(
        idx,
        addr,
        bulk_out,
        (uint8_t*)cbw,
        31,
        0,
        1
    )) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return 0;
    }

    ehci_udelay(100);

    if (!ehci_bulk_transfer_dev(
        idx,
        addr,
        bulk_out,
        (uint8_t*)buf,
        count * 512,
        0,
        0
    )) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return 0;
    }

    if (!ehci_bulk_transfer_dev(
        idx,
        addr,
        bulk_in,
        (uint8_t*)csw,
        CSW_SIZE,
        1,
        1
    )) {
        free_phys(cbw, 1);
        free_phys(csw, 1);
        return 0;
    }

    int ok =
        (csw->signature == CSW_SIGNATURE) &&
        (csw->tag == cbw->tag) &&
        (csw->status == 0);

    free_phys(cbw, 1);
    free_phys(csw, 1);

    return usb_msc_synchronize_cache(idx, addr, bulk_in, bulk_out);
}

int ehci_usb_write(int idx, uint32_t lba, uint8_t count, const void* buf) {
    if (idx >= usb_device_count) return 0;
    return usb_msc_write_10(idx, usb_devices[idx].address,
        usb_devices[idx].bulk_in_ep,
        usb_devices[idx].bulk_out_ep,
        lba, count, buf);
}

int ehci_usb_read(int idx, uint32_t lba, uint8_t count, void* buf) {
    if (idx >= usb_device_count) return 0;
    return usb_msc_read_10(idx, usb_devices[idx].address,
        usb_devices[idx].bulk_in_ep,
        usb_devices[idx].bulk_out_ep,
        lba, count, buf);
}

int ehci_usb_get_count(void) {
    return usb_device_count;
}

void ehci_usb_get_info(int idx, uint32_t* sectors, uint32_t* block_size) {
    if (idx < usb_device_count) {
        *sectors = usb_devices[idx].lba_count;
        *block_size = usb_devices[idx].block_size;
    }
}

void ehci_init_device_on_port(int port) {
    if (port_initialized[port]) return;

    ehci_port_reset(port);
    if (ehci_enumerate_port(port)) {
        port_initialized[port] = 1;
        block_add_usb_device(usb_device_count - 1);
    }
}

void ehci_remove_device(int port) {
    for (int i = 0; i < usb_device_count; i++) {
        if (usb_devices[i].port == port && usb_devices[i].present) {
            usb_devices[i].present = 0;

            block_remove_usb_device(i);

            for (int j = i; j < usb_device_count - 1; j++) {
                usb_devices[j] = usb_devices[j + 1];
            }
            usb_device_count--;

            port_initialized[port] = 0;
            break;
        }
    }
}

void ehci_poll_ports(void) {
    uint32_t hcsparams = mmio_read(ehci_cap(EHCI_HCSPARAMS));
    int max_ports = hcsparams & 0x0F;

    for (int port = 0; port < max_ports; port++) {
        uint32_t portsc = mmio_read(ehci_portsc(port));

        if (portsc != last_portsc[port]) {
            if ((portsc & PORTSC_CCS) && !(last_portsc[port] & PORTSC_CCS)) {

                if (ehci_port_reset(port)) {
                    if (ehci_enumerate_port(port)) {
                        port_initialized[port] = 1;
                    }
                }
            }
            else if (!(portsc & PORTSC_CCS) && (last_portsc[port] & PORTSC_CCS)) {
                ehci_remove_device(port);
            }

            last_portsc[port] = portsc;
        }
    }
}

void ehci_init(void) {
    static int already_init = 0;

    if (already_init) {
        return;
    }

    if (!ehci_find_controller()) {
        print("EHCI: Controller not found\n");
        return;
    }

    uint8_t caplen = mmio_read8(ehci_cap(EHCI_CAPLENGTH));
    uint16_t hci_version = mmio_read16(ehci_cap(EHCI_HCIVERSION));
    uint32_t hcsparams = mmio_read(ehci_cap(EHCI_HCSPARAMS));

    int max_ports = hcsparams & 0x0F;
    int real_ports = 0;

    ehci_port_count = 0;
    for (int port = 0; port < max_ports; port++) {
        if (mmio_read(ehci_portsc(port)) & PORTSC_CCS) {
            ehci_port_count++;
        }
    }

    for (int port = 0; port < max_ports; port++) {
        uint32_t status = mmio_read(ehci_portsc(port));
        if (status & PORTSC_CCS) {
            real_ports++;
        }
    }

    if (!ehci_reset_controller()) {
        print("EHCI: Reset failed\n");
        return;
    }

    if (!ehci_init_frame_list()) {
        print("EHCI: Frame list init failed\n");
        return;
    }

    if (!ehci_init_async_qh()) {
        print("EHCI: Async QH init failed\n");
        return;
    }

    ehci_configure_ports();
    ehci_run_controller();
    ehci_enable_async_schedule();

    for (int port = 0; port < max_ports && usb_device_count < MAX_DEVICES; port++) {
        uint32_t status = mmio_read(ehci_portsc(port));

        if (!(status & PORTSC_CCS)) {
            continue;
        }

        if (port_initialized[port]) {
            continue;
        }

        if (ehci_port_reset(port)) {
            if (ehci_enumerate_port(port)) {
                port_initialized[port] = 1;
            }
        }
    }

    already_init = 1;
    qh_initialized = 1;
}