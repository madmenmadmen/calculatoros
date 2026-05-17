section .text

global irq0
global irq1
global irq12

extern pit_handler
extern keyboard_handler
extern mouse_handler

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