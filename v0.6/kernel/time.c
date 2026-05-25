#include "time.h"
#include "console.h"
#include <stdint.h>

/* CMOS RTC is accessed via ports 0x70 (index) and 0x71 (data).
   Write the register number to 0x70, read the value from 0x71.
   Values are in BCD by default: upper nibble = tens, lower = units. */

#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04

static inline uint8_t cmos_read(uint8_t reg) {
    uint8_t val;
    __asm__ volatile (
        "outb %%al, %1\n"
        "inb  %2, %%al\n"
        : "=a"(val)
        : "N"((uint8_t)CMOS_ADDR), "N"((uint8_t)CMOS_DATA), "0"(reg)
    );
    return val;
}

/* BCD to binary: 0x59 -> 59 */
static uint8_t bcd(uint8_t v) {
    return (v >> 4) * 10 + (v & 0x0F);
}

static void print_two(uint8_t n) {
    console_putchar('0' + n / 10);
    console_putchar('0' + n % 10);
}

void cmd_time(void) {
    uint8_t h = bcd(cmos_read(RTC_HOURS));
    uint8_t m = bcd(cmos_read(RTC_MINUTES));
    uint8_t s = bcd(cmos_read(RTC_SECONDS));

    /* Adjust for timezone — change this offset to match yours */
    #define TZ_OFFSET_HOURS  -6    /* MDT = UTC-6, MST = UTC-7 */
    int lh = (int)h + TZ_OFFSET_HOURS;
    if (lh < 0)  lh += 24;
    if (lh >= 24) lh -= 24;

    console_print("Time: ");
    print_two((uint8_t)lh); console_putchar(':');
    print_two(m);            console_putchar(':');
    print_two(s);
    console_print(" MDT\n");
}