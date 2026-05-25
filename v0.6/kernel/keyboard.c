#include "keyboard.h"
#include "console.h"
#include "pic.h"
#include <stdint.h>

static uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(ret) : "d"(port));
    return ret;
}

/* Scancode to ASCII table for US keyboard layout (key press, not release) */
static const char scancode_table[128] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b', /* 0x00-0x0E */
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',       /* 0x0F-0x1C */
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',           /* 0x1D-0x29 */
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,             /* 0x2A-0x36 */
    '*', 0,  ' '                                                      /* 0x37-0x39 */
};

static int shift_pressed = 0;

static char apply_shift(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c == '1') return '!';
    if (c == '2') return '@';
    if (c == '3') return '#';
    if (c == '4') return '$';
    if (c == '5') return '%';
    if (c == '6') return '^';
    if (c == '7') return '&';
    if (c == '8') return '*';
    if (c == '9') return '(';
    if (c == '0') return ')';
    if (c == '-') return '_';
    if (c == '=') return '+';
    if (c == '[') return '{';
    if (c == ']') return '}';
    if (c == ';') return ':';
    if (c == '\'') return '"';
    if (c == ',') return '<';
    if (c == '.') return '>';
    if (c == '/') return '?';
    if (c == '\\') return '|';
    if (c == '`') return '~';
    return c;
}

void keyboard_handler() {
    uint8_t scancode = inb(0x60);   /* Read scancode from keyboard port */

    /* Key release events have bit 7 set */
    if (scancode & 0x80) {
        scancode &= 0x7F;
        if (scancode == 0x2A || scancode == 0x36)
            shift_pressed = 0;      /* Left or right shift released */
        pic_send_eoi(1);
        return;
    }

    /* Shift pressed */
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        pic_send_eoi(1);
        return;
    }

    /* Look up ASCII value */
    if (scancode < 128) {
        char c = scancode_table[scancode];
        if (c != 0) {
            if (shift_pressed && c != '\n' && c != '\b' && c != '\t')
                c = apply_shift(c);
            console_handle_input(c);
        }
    }

    pic_send_eoi(1);
}

void keyboard_init() {
    /* Nothing needed here — IDT entry is set up in idt.c */
}