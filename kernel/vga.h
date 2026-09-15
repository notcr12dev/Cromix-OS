#ifndef VGA_H
#define VGA_H

/* ─────────────────────────────────────────────────────────────
 * DEV-OS · driver mínimo VGA texto 80x25 (modo 3).
 * Escribe directo en 0xB8000. Sin dependencias de libc.
 * ───────────────────────────────────────────────────────────── */
#include <stddef.h>
#include <stdint.h>

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_MEM  ((volatile uint16_t *)0xB8000)

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE = 1, VGA_GREEN = 2, VGA_CYAN = 3,
    VGA_RED = 4, VGA_MAGENTA = 5, VGA_BROWN = 6, VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11, VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW = 14, VGA_WHITE = 15,
};

static inline uint8_t vga_attr(enum vga_color fg, enum vga_color bg)
{
    return (uint8_t)((bg << 4) | (fg & 0x0F));
}

static inline uint16_t vga_cell(char c, uint8_t attr)
{
    return (uint16_t)(((uint16_t)attr << 8) | (uint8_t)c);
}

#endif /* VGA_H */
