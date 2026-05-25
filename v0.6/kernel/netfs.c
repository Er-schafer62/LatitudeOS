/* netfs.c — Simple network filesystem over serial
 *
 * Protocol:
 *   GET <filename>\n   ->  uint32 LE size + N bytes  (0xFFFFFFFF = not found)
 *   LIST\n             ->  uint32 LE size + newline-separated filenames
 *   PUT <filename>\n   ->  OS sends uint32 LE size + N bytes data
 *                          Server responds: 1 byte, 'K' = OK, 'E' = error
 */

#include "netfs.h"
#include "serial.h"
#include <stdint.h>

#define NOT_FOUND_SENTINEL  0xFFFFFFFF
#define TIMEOUT_LOOPS       500000

void netfs_init(void) {
    serial_init();
}

/* Send a uint32 in little-endian over serial */
static void send_uint32(uint32_t val) {
    serial_putchar((char)( val        & 0xFF));
    serial_putchar((char)((val >>  8) & 0xFF));
    serial_putchar((char)((val >> 16) & 0xFF));
    serial_putchar((char)((val >> 24) & 0xFF));
}

/* Wait for first byte with timeout, then read remaining 3 bytes of uint32. */
static int recv_uint32(uint32_t* out) {
    uint16_t lsr_port = 0x3F8 + 5;
    uint8_t  lsr;
    int timeout = TIMEOUT_LOOPS;

    do {
        __asm__ volatile ("inb %%dx, %%al" : "=a"(lsr) : "d"(lsr_port));
        if (--timeout <= 0) return -1;
    } while (!(lsr & 0x01));

    uint8_t bytes[4];
    __asm__ volatile ("inb %%dx, %%al"
                      : "=a"(bytes[0])
                      : "d"((uint16_t)0x3F8));
    bytes[1] = (uint8_t)serial_getchar();
    bytes[2] = (uint8_t)serial_getchar();
    bytes[3] = (uint8_t)serial_getchar();

    *out = (uint32_t)bytes[0]
         | ((uint32_t)bytes[1] <<  8)
         | ((uint32_t)bytes[2] << 16)
         | ((uint32_t)bytes[3] << 24);
    return 0;
}

int netfs_get(const char* filename, uint8_t* buf, uint32_t buf_size) {
    serial_print("GET ");
    serial_print(filename);
    serial_putchar('\n');

    uint32_t file_size;
    if (recv_uint32(&file_size) < 0)       return NETFS_ERR_TIMEOUT;
    if (file_size == NOT_FOUND_SENTINEL)   return NETFS_ERR_NOTFOUND;
    if (file_size > buf_size)              return NETFS_ERR_TOOBIG;

    if (file_size > 0)
        serial_read(buf, file_size);

    return (int)file_size;
}

int netfs_list(uint8_t* buf, uint32_t buf_size) {
    serial_print("LIST\n");

    uint32_t resp_size;
    if (recv_uint32(&resp_size) < 0)       return NETFS_ERR_TIMEOUT;
    if (resp_size == NOT_FOUND_SENTINEL)   return NETFS_ERR_TIMEOUT;
    if (resp_size > buf_size)              return NETFS_ERR_TOOBIG;

    if (resp_size > 0)
        serial_read(buf, resp_size);

    return (int)resp_size;
}

int netfs_put(const char* filename, const uint8_t* buf, uint32_t size) {
    /* Send: PUT <filename>\n */
    serial_print("PUT ");
    serial_print(filename);
    serial_putchar('\n');

    /* Send: 4-byte file size */
    send_uint32(size);

    /* Send: file data */
    for (uint32_t i = 0; i < size; i++)
        serial_putchar((char)buf[i]);

    /* Wait for server acknowledgement: 'K' = OK, anything else = error */
    uint16_t lsr_port = 0x3F8 + 5;
    uint8_t  lsr;
    int timeout = TIMEOUT_LOOPS;

    do {
        __asm__ volatile ("inb %%dx, %%al" : "=a"(lsr) : "d"(lsr_port));
        if (--timeout <= 0) return NETFS_ERR_TIMEOUT;
    } while (!(lsr & 0x01));

    char ack = serial_getchar();
    if (ack != 'K') return NETFS_ERR_REFUSED;

    return NETFS_OK;
}