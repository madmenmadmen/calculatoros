# Compiler and flags
CC = i686-elf-gcc
CFLAGS = -ffreestanding -Wall -Wextra -I.
ASM = nasm
ASMFLAGS = -felf32
LD = i686-elf-ld
LDFLAGS = -T linker.ld

# Directories
ISO_DIR = iso
GRUB_DIR = $(ISO_DIR)/boot/grub

# C source files
C_SOURCES = kernel.c keyboard_buffer.c pic.c pit.c idt.c jit.c fs.c ata.c block.c pci.c ahci.c ehci.c

# Assembly source files
ASM_SOURCES = irq.asm idt.asm idt_asm.asm boot.asm exceptions.asm

# Object files
C_OBJS = $(C_SOURCES:.c=.o)
ASM_OBJS = $(ASM_SOURCES:.asm=.o)
OBJS = $(C_OBJS) $(ASM_OBJS)

# Kernel and ISO
KERNEL = kernel.bin
OS_ISO = os.iso

# QEMU options
QEMU = qemu-system-i386
QEMU_FLAGS = -m 512M -no-reboot -no-shutdown

# Disk images
DISK_IMG = disk.img
SATA_IMG = sata.img
USB_IMG = usb.img
USB2_IMG = usb2.img

# Default target
all: $(KERNEL) $(OS_ISO)

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile ASM files
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# Link kernel
$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# Create OS ISO
$(OS_ISO): $(KERNEL) grub.cfg
	mkdir -p $(GRUB_DIR)
	cp $(KERNEL) $(ISO_DIR)/boot/
	cp grub.cfg $(GRUB_DIR)/
	grub-mkrescue -o $@ $(ISO_DIR)

# Create empty disk images
$(DISK_IMG):
	dd if=/dev/zero of=$@ bs=1M count=5000

$(SATA_IMG):
	dd if=/dev/zero of=$@ bs=1M count=5000

# Create USB disk images
$(USB_IMG):
	dd if=/dev/zero of=$@ bs=1M count=100

$(USB2_IMG):
	dd if=/dev/zero of=$@ bs=1M count=100

# Setup MBR partitions
setup-disk: $(DISK_IMG)
	python3 mkmbr.py $(DISK_IMG) 1 1000

setup-disk-sata: $(SATA_IMG)
	python3 mkmbr.py $(SATA_IMG) 1 1000

setup-disk-usb: $(USB_IMG) $(USB2_IMG)
	python3 mkmbr.py $(USB_IMG) 1 50 2>/dev/null || true
	python3 mkmbr.py $(USB2_IMG) 1 50 2>/dev/null || true
	@echo "USB images created: $(USB_IMG), $(USB2_IMG)"

# Run with HDD + SATA + EHCI (USB)
run: $(OS_ISO) $(DISK_IMG) $(SATA_IMG) $(USB_IMG) $(USB2_IMG)
	$(QEMU) \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-drive file=$(OS_ISO),format=raw,if=ide,index=3,media=cdrom \
		-boot d \
		-device ahci,id=ahci \
		-drive file=$(SATA_IMG),format=raw,if=none,id=sata0 \
		-device ide-hd,drive=sata0,bus=ahci.0 \
		-device usb-ehci,id=ehci \
		-drive file=$(USB_IMG),format=raw,if=none,id=usb0 \
		-device usb-storage,drive=usb0,bus=ehci.0,id=usb-device0 \
		-drive file=$(USB2_IMG),format=raw,if=none,id=usb1 \
		-device usb-storage,drive=usb1,bus=ehci.0,id=usb-device1 \
		-d int -D qemu_int.log \
		$(QEMU_FLAGS)

# Run with only USB (no SATA, no IDE)
run-usb: $(OS_ISO) $(USB_IMG) $(USB2_IMG)
	$(QEMU) \
		-drive file=$(OS_ISO),format=raw,if=ide,index=0,media=cdrom \
		-boot d \
		-device usb-ehci,id=ehci \
		-drive file=$(USB_IMG),format=raw,if=none,id=usb0 \
		-device usb-storage,drive=usb0,bus=ehci.0 \
		-drive file=$(USB2_IMG),format=raw,if=none,id=usb1 \
		-device usb-storage,drive=usb1,bus=ehci.0 \
		$(QEMU_FLAGS)

# Run with all devices (ATA + SATA + EHCI USB)
run-all: $(OS_ISO) $(DISK_IMG) $(SATA_IMG) $(USB_IMG) $(USB2_IMG)
	$(QEMU) \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-drive file=$(OS_ISO),format=raw,if=ide,index=3,media=cdrom \
		-boot d \
		-device ahci,id=ahci \
		-drive file=$(SATA_IMG),format=raw,if=none,id=sata0 \
		-device ide-hd,drive=sata0,bus=ahci.0 \
		-drive file=$(SATA_IMG)2,format=raw,if=none,id=sata1 \
		-device ide-hd,drive=sata1,bus=ahci.1 \
		-device usb-ehci,id=ehci \
		-drive file=$(USB_IMG),format=raw,if=none,id=usb0 \
		-device usb-storage,drive=usb0,bus=ehci.0,port=1 \
		-drive file=$(USB2_IMG),format=raw,if=none,id=usb1 \
		-device usb-storage,drive=usb1,bus=ehci.0,port=2 \
		-monitor vc \
		$(QEMU_FLAGS)

# Run with debug output
rund: $(OS_ISO) $(DISK_IMG) $(SATA_IMG) $(USB_IMG)
	$(QEMU) \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0 \
		-drive file=$(OS_ISO),format=raw,if=ide,index=3,media=cdrom \
		-boot d \
		-device ahci,id=ahci \
		-drive file=$(SATA_IMG),format=raw,if=none,id=sata0 \
		-device ide-hd,drive=sata0,bus=ahci.0 \
		-device usb-ehci,id=ehci \
		-drive file=$(USB_IMG),format=raw,if=none,id=usb0 \
		-device usb-storage,drive=usb0,bus=ehci.0,port=1 \
		-trace usb\* \
		-D qemu.log \
		$(QEMU_FLAGS)

# Clean
clean:
	rm -f *.o $(KERNEL) $(OS_ISO)
	rm -rf $(ISO_DIR)

# Clean all including disk images
clean-all: clean
	rm -f $(DISK_IMG) $(SATA_IMG) $(USB_IMG) $(USB2_IMG)

.PHONY: all run run-usb run-all rund clean clean-all setup-disk setup-disk-sata setup-disk-usb