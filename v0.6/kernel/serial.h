#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/* Initialise COM1 (0x3F8) at 9600 baud, 8N1 */
void serial_init(void);

/* Send a single byte — blocks until UART ready */
void serial_putchar(char c);

/* Send a null-terminated string */
void serial_print(const char* s);

/* Receive a single byte — blocks until data arrives */
char serial_getchar(void);

/* Read exactly 'len' bytes into buf — blocks until all received */
void serial_read(uint8_t* buf, uint32_t len);

#endif
