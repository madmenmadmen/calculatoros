section .text

global irq_hpet
global irq1
global irq12

extern keyboard_handler
extern mouse_handler
extern hpet_handler

irq_hpet:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call hpet_handler
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

irq1:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call keyboard_handler

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd


irq12:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call mouse_handler

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd