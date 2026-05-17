section .text
global start
extern kernel_main

; =========================
; GDT
; =========================

gdt_start:
    ; 0x00 - null descriptor
    dq 0x0000000000000000

    ; 0x08 - code segment: base=0, limit=4GB
    dq 0x00CF9A000000FFFF

    ; 0x10 - data segment (большой): base=0, limit=4GB
    dq 0x00CF92000000FFFF

    ; 0x18 - stack segment (маленький): base=0, limit=64KB (0xFFFF)
    dq 0x000092000000FFFF

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; =========================
; ENTRY
; =========================

start:
    cli

    ; load GDT
    lgdt [gdt_descriptor]

    ; reload segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; far jump to flush CS
    jmp 0x08:flush_cs

flush_cs:
    mov esp, stack_top

    push ebx
    push eax
    call kernel_main

hang:
    cli
    hlt
    jmp hang

section .bss
align 16
stack_bottom:
    resb 8192
stack_top: