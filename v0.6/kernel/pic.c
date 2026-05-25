#include "pic.h"
#include <stdint.h>

#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

#define ICW1_INIT   0x11
#define ICW4_8086   0x01

static void io_wait() {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

static void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %%al, %%dx" : : "a"(val), "d"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(ret) : "d"(port));
    return ret;
}

void pic_init() {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD,  ICW1_INIT); io_wait();
    outb(PIC2_CMD,  ICW1_INIT); io_wait();

    outb(PIC1_DATA, 0x20);      io_wait();
    outb(PIC2_DATA, 0x28);      io_wait();

    outb(PIC1_DATA, 0x04);      io_wait();
    outb(PIC2_DATA, 0x02);      io_wait();

    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Suppress unused variable warnings — masks saved for potential restore */
    (void)mask1;
    (void)mask2;

    outb(PIC1_DATA, 0xFD);  /* Only IRQ1 (keyboard) unmasked */
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(int irq) {
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
