; boot/stage2.asm
;
; Memory layout after stage2:
;   0x10000  kernel  (LBA 5,  48 sectors = 24KB max)
;   0x20000  ramdisk (LBA 54, 64 sectors = FAT12 volume)
;
; Uses INT 0x13 AH=0x42 (LBA extensions) — no CHS geometry needed.
;
; IMPORTANT: these LBA values must match mkimage.py and disk.c:
;   KERNEL_START_LBA  = 5
;   KERNEL_SECTORS    = 48
;   FAT12_START_LBA   = 54

[BITS 16]
[ORG 0x7E00]

%define KERNEL_LBA        5
%define KERNEL_SECTORS    48          ; matches mkimage.py
%define KERNEL_SEG        0x1000

%define RAMDISK_LBA       54          ; matches mkimage.py
%define RAMDISK_SECTORS   64
%define RAMDISK_SEG       0x2000

START16:
    mov [boot_drive], dl

    mov si, msg_stage2
    call print16

    ; ── Load kernel: LBA 5, 48 sectors -> 0x1000:0000 (linear 0x10000) ───────
    mov si, msg_loading_kernel
    call print16

    mov word  [dap_sectors],  KERNEL_SECTORS
    mov word  [dap_offset],   0x0000
    mov word  [dap_segment],  KERNEL_SEG
    mov dword [dap_lba_low],  KERNEL_LBA
    mov dword [dap_lba_high], 0

    mov dl, [boot_drive]
    mov ah, 0x42
    mov si, dap
    int 0x13
    jc disk_error

    mov si, msg_kernel_ok
    call print16

    ; ── Load FAT12 ramdisk: LBA 54, 64 sectors -> 0x2000:0000 (linear 0x20000)
    mov si, msg_loading_fs
    call print16

    mov word  [dap_sectors],  RAMDISK_SECTORS
    mov word  [dap_offset],   0x0000
    mov word  [dap_segment],  RAMDISK_SEG
    mov dword [dap_lba_low],  RAMDISK_LBA
    mov dword [dap_lba_high], 0

    mov dl, [boot_drive]
    mov ah, 0x42
    mov si, dap
    int 0x13
    jc disk_error

    mov si, msg_fs_ok
    call print16

    ; ── Enable A20: BIOS method, fast A20 fallback ───────────────────────────
    mov ax, 0x2401
    int 0x15
    jnc .a20_done
    in  al, 0x92
    or  al, 2
    and al, 0xFE
    out 0x92, al
.a20_done:

    ; ── Enter protected mode ──────────────────────────────────────────────────
    lgdt [gdt_descriptor]
    cli
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:START32

disk_error:
    mov si, msg_disk_error
    call print16
    cli
    hlt

print16:
    mov ah, 0x0E
    mov bh, 0
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

; ── Disk Address Packet ───────────────────────────────────────────────────────
align 2
dap:
    db 0x10
    db 0x00
dap_sectors:  dw 0
dap_offset:   dw 0
dap_segment:  dw 0
dap_lba_low:  dd 0
dap_lba_high: dd 0

; ── GDT ──────────────────────────────────────────────────────────────────────
gdt_start:
gdt_null: dq 0
gdt_code: dq 0x00CF9A000000FFFF
gdt_data: dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_drive          db 0
msg_stage2          db "Stage2 OK", 13, 10, 0
msg_loading_kernel  db "Loading kernel...", 13, 10, 0
msg_kernel_ok       db "Kernel OK", 13, 10, 0
msg_loading_fs      db "Loading filesystem...", 13, 10, 0
msg_fs_ok           db "FS OK", 13, 10, 0
msg_disk_error      db "DISK ERROR - halted", 13, 10, 0

; ── 32-bit entry ─────────────────────────────────────────────────────────────
[BITS 32]
START32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp 0x10000

times 2048-($-$$) db 0