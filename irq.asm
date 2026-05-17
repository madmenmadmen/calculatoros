global irq0
global irq1

extern pit_handler
extern keyboard_handler

irq0:
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

    call pit_handler

    pop gs
    pop fs
    pop es
    pop ds
    popa

    iret

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

    iret