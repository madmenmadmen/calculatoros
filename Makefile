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
C_SOURCES = kernel.c keyboard_buffer.c apic.c idt.c jit.c fs.c block.c pci.c ahci.c acpi.c hpet.c

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
QEMU = qemu-system-x86_64
QEMU_FLAGS = -m 2G

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

$(USB_IMG):
	dd if=/dev/zero of=$@ bs=1M count=100

$(USB2_IMG):
	dd if=/dev/zero of=$@ bs=1M count=100

# Setup MBR partitions
setup-disk: $(DISK_IMG)
	python3 mkmbr.py $(DISK_IMG) 1 1000

setup-disk-usb: $(USB_IMG) $(USB2_IMG)
	python3 mkmbr.py $(USB_IMG) 1 50 2>/dev/null || true
	python3 mkmbr.py $(USB2_IMG) 1 50 2>/dev/null || true
	@echo "USB images created: $(USB_IMG), $(USB2_IMG)"

run: $(OS_ISO) $(DISK_IMG)
	$(QEMU) \
		-machine q35 \
		-drive file=$(OS_ISO),format=raw,if=ide,index=3,media=cdrom \
		-boot d \
		-device ahci,id=ahci \
		-drive file=$(DISK_IMG),format=raw,if=none,id=sata0 \
		-device ide-hd,drive=sata0,bus=ahci.0 \
		-serial file:serial.log \
		$(QEMU_FLAGS)

# Clean
clean:
	rm -f *.o $(KERNEL) $(OS_ISO)
	rm -rf $(ISO_DIR)

# Clean all including disk images
clean-all: clean
	rm -f $(DISK_IMG) $(SATA_IMG) $(USB_IMG) $(USB2_IMG)

.PHONY: all run run-nvme run-nvme-mq run-all run-nvme-debug clean clean-all setup-disk setup-disk-usb