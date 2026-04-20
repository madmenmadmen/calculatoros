all:
	i686-elf-gcc -ffreestanding -c kernel.c -o kernel.o
	i686-elf-gcc -ffreestanding -c keyboard_buffer.c -o keyboard_buffer.o
	i686-elf-gcc -ffreestanding -c pic.c -o pic.o
	i686-elf-gcc -ffreestanding -c pit.c -o pit.o
	i686-elf-gcc -ffreestanding -c idt.c -o idt.o
	nasm -felf32 irq.asm -o irq.o
	nasm -felf32 idt.asm -o idt_asm.o
	i686-elf-ld -T linker.ld -o kernel.bin kernel.o pit.o pic.o keyboard_buffer.o idt.o irq.o idt_asm.o

	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/
	cp grub.cfg iso/boot/grub/

	grub-mkrescue -o os.iso iso

run:
	qemu-system-i386 -usb -device usb-ehci -cdrom os.iso

clean:
	rm -f *.o kernel.bin os.iso
	rm -rf iso