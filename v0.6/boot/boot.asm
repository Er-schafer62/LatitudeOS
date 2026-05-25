; boot/boot.asm
[BITS 16]
[ORG 0x7C00]

start:
    ; BIOS puts the boot drive number in DL — preserve it!
    mov [boot_drive], dl

    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00      ; Stack grows down from bootloader

    mov si, msg_loading
    call print

    ; Load stage2 (4 sectors) to 0x0000:0x7E00
    mov bx, 0x7E00      ; Load destination
    mov ah, 0x02        ; BIOS: read sectors
    mov al, 4           ; Number of sectors to read
    mov ch, 0           ; Cylinder 0
    mov cl, 2           ; Start at sector 2 (sector 1 = this bootloader)
    mov dh, 0           ; Head 0
    mov dl, [boot_drive]; Use the actual boot drive
    int 0x13
    jc disk_error

    ; Pass boot drive in DL to stage2
    mov dl, [boot_drive]
    jmp 0x0000:0x7E00

disk_error:
    mov si, msg_error
    call print
    cli
    hlt

print:
    mov ah, 0x0E
    mov bh, 0
.print_loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .print_loop
.done:
    ret

boot_drive  db 0
msg_loading db "Loading MyOS...", 13, 10, 0
msg_error   db "Disk read error!", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
