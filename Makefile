all:
	i686-elf-gcc -ffreestanding -c kernel.c -o kernel.o
	i686-elf-ld -T linker.ld -o kernel.bin kernel.o

	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/
	cp grub.cfg iso/boot/grub/

	grub-mkrescue -o os.iso iso

run:
	qemu-system-i386 -usb -device usb-ehci -cdrom os.iso

clean:
	rm -f *.o kernel.bin os.iso
	rm -rf iso
