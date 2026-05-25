[BITS 32]
[GLOBAL KERNEL_START]
[GLOBAL keyboard_isr]
[EXTERN kernel_main]
[EXTERN keyboard_handler]

KERNEL_START:
    cli
    mov esp, 0x90000
    call kernel_main

.hang:
    hlt
    jmp .hang

; Keyboard interrupt service routine
; Called by CPU when IRQ1 fires (keyboard)
keyboard_isr:
    pusha                   ; Save all registers
    call keyboard_handler   ; Call our C handler
    popa                    ; Restore all registers
    iret                    ; Return from interrupt