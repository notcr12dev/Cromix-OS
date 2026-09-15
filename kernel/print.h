#ifndef PRINT_H
#define PRINT_H

/* Cronix OS · VGA text 80x25 + COM1 serial output. No libc. */
#include <stddef.h>
#include <stdint.h>

void print_init(void);             /* COM1 serial 38400 8N1 */
void vga_clear(void);
void k_color(uint8_t fg, uint8_t bg);
void k_putc(char c);               /* one char to VGA + serial */
void k_print(const char *s);
void k_backspace(void);            /* erase last visible char */
void k_print_hex64(uint64_t v);    /* 0x0123ABCDEF... format */

#endif /* PRINT_H */
