/* serial.c — COM1 UART driver (8250/16550 compatible)
 *
 * COM1 port base: 0x3F8
 * Registers (offset from base):
 *   +0  Data register (read=RX, write=TX) / Divisor latch low (DLAB=1)
 *   +1  Interrupt enable              / Divisor latch high (DLAB=1)
 *   +2  Interrupt ID / FIFO control
 *   +3  Line control (sets DLAB bit, word length, parity, stop bits)
 *   +4  Modem control
 *   +5  Line status (bit 0 = data ready, bit 5 = TX empty)
 *   +6  Modem status
 */

#include "serial.h"
#include <stdint.h>

#define COM1_BASE   0x3F8

#define COM1_DATA   (COM1_BASE + 0)
#define COM1_IER    (COM1_BASE + 1)
#define COM1_FCR    (COM1_BASE + 2)
#define COM1_LCR    (COM1_BASE + 3)
#define COM1_MCR    (COM1_BASE + 4)
#define COM1_LSR    (COM1_BASE + 5)

#define LSR_DATA_READY  0x01    /* Bit 0: received data waiting */
#define LSR_TX_EMPTY    0x20    /* Bit 5: transmit buffer empty */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %%al, %%dx" : : "a"(val), "d"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(ret) : "d"(port));
    return ret;
}

void serial_init(void) {
    /* Disable interrupts — we poll */
    outb(COM1_IER, 0x00);

    /* Enable DLAB (Divisor Latch Access Bit) to set baud rate */
    outb(COM1_LCR, 0x80);

    /* Set divisor for 9600 baud
     * Divisor = 115200 / baud = 115200 / 9600 = 12 = 0x000C */
    outb(COM1_DATA, 0x0C);   /* Divisor low byte  */
    outb(COM1_IER,  0x00);   /* Divisor high byte */

    /* 8 data bits, no parity, 1 stop bit (8N1), disable DLAB */
    outb(COM1_LCR, 0x03);

    /* Enable and clear FIFO, 14-byte threshold */
    outb(COM1_FCR, 0xC7);

    /* Enable RTS, DTR (tell the other side we're ready) */
    outb(COM1_MCR, 0x03);
}

void serial_putchar(char c) {
    /* Wait until transmit buffer is empty */
    while (!(inb(COM1_LSR) & LSR_TX_EMPTY));
    outb(COM1_DATA, (uint8_t)c);
}

void serial_print(const char* s) {
    while (*s)
        serial_putchar(*s++);
}

char serial_getchar(void) {
    /* Wait until data is available */
    while (!(inb(COM1_LSR) & LSR_DATA_READY));
    return (char)inb(COM1_DATA);
}

void serial_read(uint8_t* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        buf[i] = (uint8_t)serial_getchar();
}
