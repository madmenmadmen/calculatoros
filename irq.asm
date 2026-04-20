global irq0
global irq1

extern pit_handler
extern keyboard_handler
extern pic_send_eoi

irq0:
    cli
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

    push 0
    call pic_send_eoi
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds

    popa
    sti
    iret

irq1:
    cli
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

    push 1
    call pic_send_eoi
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds

    popa
    sti
    iret