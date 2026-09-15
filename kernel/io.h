#ifndef IO_H
#define IO_H

/* Cronix OS · x86 ports (inb/outb). Inline-only header. */
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void io_wait(void)
{
    outb(0x80, 0); /* unused port: short delay */
}

#endif /* IO_H */
