#ifndef PRINT_H
#define PRINT_H

/* DEV-OS · salida VGA texto 80x25 + serie COM1. Sin libc. */
#include <stddef.h>
#include <stdint.h>

void print_init(void);             /* serie COM1 38400 8N1 */
void vga_clear(void);
void k_color(uint8_t fg, uint8_t bg);
void k_putc(char c);               /* un char a VGA + serie */
void k_print(const char *s);
void k_backspace(void);            /* borra último char visible */
void k_print_hex64(uint64_t v);    /* formato 0x0123ABCDEF... */

#endif /* PRINT_H */
