#include "idt.h"
#include "keyboard.h"
#include <stdint.h>

/* Each IDT entry is 8 bytes */
struct idt_entry {
    uint16_t offset_low;    /* Lower 16 bits of handler address */
    uint16_t selector;      /* Code segment selector (0x08 from our GDT) */
    uint8_t  zero;          /* Always 0 */
    uint8_t  type_attr;     /* Type and attributes */
    uint16_t offset_high;   /* Upper 16 bits of handler address */
} __attribute__((packed));

struct idt_descriptor {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_ENTRIES 256
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_descriptor idt_desc;

/* The keyboard ISR is defined in start.asm */
extern void keyboard_isr();

static void idt_set_entry(int n, uint32_t handler) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;          /* Kernel code segment */
    idt[n].zero        = 0;
    idt[n].type_attr   = 0x8E;          /* Present, Ring 0, 32-bit interrupt gate */
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_init() {
    /* Clear all entries */
    for (int i = 0; i < IDT_ENTRIES; i++)
        idt_set_entry(i, 0);

    /* IRQ1 = keyboard, remapped to interrupt 0x21 by the PIC */
    idt_set_entry(0x21, (uint32_t)keyboard_isr);

    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base  = (uint32_t)idt;

    __asm__ volatile ("lidt %0" : : "m"(idt_desc));
}