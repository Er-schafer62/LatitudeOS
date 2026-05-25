#include "console.h"
#include "pic.h"
#include "idt.h"
#include "keyboard.h"
#include "fat12.h"
#include "serial.h"
#include "netfs.h"
#include <stdint.h>

#define FAT12_LBA 38

static void print_hex_byte(uint8_t b) {
    const char* hex = "0123456789ABCDEF";
    char s[3];
    s[0] = hex[b >> 4];
    s[1] = hex[b & 0xF];
    s[2] = '\0';
    console_print(s);
}

void kernel_main() {
    console_init();
    pic_init();
    idt_init();
    keyboard_init();

    /* Debug: verify ramdisk loaded correctly by stage2 */
    console_print("Ramdisk @ 0x20000: ");
    uint8_t* rd = (uint8_t*)0x20000;
    for (int i = 0; i < 8; i++) {
        print_hex_byte(rd[i]);
        console_putchar(' ');
    }
    console_newline();
    /* Expected: EB 3C 90 4D 59 4F 53 30  (jump + "MYOS0") */

    int fat_result = fat12_init(FAT12_LBA);
    if (fat_result == FAT12_OK)
        console_print("FAT12 mounted OK.\n");
    else {
        console_print("FAT12 mount failed (code: ");
        print_hex_byte((uint8_t)(-fat_result));
        console_print(").\n");
    }

    netfs_init();
    console_print("Serial OK. 'get <file>' to fetch from server.\n");

    __asm__ volatile ("sti");
    while (1) { __asm__ volatile ("hlt"); }
}