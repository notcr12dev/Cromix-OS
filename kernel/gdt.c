/* Cronix OS · gdt.c: own table, reload segments via gdt_flush.
 * Selectors: 0x08 = code, 0x10 = data (match stage2). */
#include "gdt.h"

#include <stdint.h>

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void gdt_flush(uint64_t ptr);

static uint64_t g_gdt[3];

void gdt_init(void)
{
    g_gdt[0] = 0x0000000000000000ULL; /* null */
    g_gdt[1] = 0x00209A0000000000ULL; /* 64-bit code: L=1, exec, readable */
    g_gdt[2] = 0x0000920000000000ULL; /* data: present, writable */

    struct gdt_ptr p;
    p.limit = (uint16_t)(sizeof(g_gdt) - 1);
    p.base = (uint64_t)&g_gdt;
    gdt_flush((uint64_t)&p);
}
