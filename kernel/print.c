/* Cronix OS · print.c: VGA + COM1. Everything goes out both
 * sides: serial is the log you see with `-serial stdio` in QEMU. */
#include "print.h"

#include "io.h"
#include "vga.h"

#define COM1 0x3F8u

static size_t g_row;
static size_t g_col;
static uint8_t g_attr;

static int serial_ready(void)
{
    return (inb(COM1 + 5) & 0x20) != 0;
}

static void serial_putc(char c)
{
    while (!serial_ready()) {
        /* busy-wait */
    }
    outb(COM1, (uint8_t)c);
}

void print_init(void)
{
    outb(COM1 + 1, 0x00); /* IRQ off */
    outb(COM1 + 3, 0x80); /* DLAB on */
    outb(COM1 + 0, 0x03); /* 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFO */
    outb(COM1 + 4, 0x0B); /* RTS+DTR */
    g_attr = vga_attr(VGA_LIGHT_GREY, VGA_BLACK);
}

void k_color(uint8_t fg, uint8_t bg)
{
    g_attr = vga_attr(fg, bg);
}

void vga_clear(void)
{
    for (size_t y = 0; y < VGA_ROWS; y++) {
        for (size_t x = 0; x < VGA_COLS; x++) {
            VGA_MEM[y * VGA_COLS + x] = vga_cell(' ', g_attr);
        }
    }
    g_row = 0;
    g_col = 0;
}

void k_putc(char c)
{
    if (c == '\n') {
        g_col = 0;
        if (++g_row >= VGA_ROWS) {
            g_row = VGA_ROWS - 1; /* no scroll: sticks at bottom */
        }
        serial_putc('\r');
        serial_putc('\n');
        return;
    }
    VGA_MEM[g_row * VGA_COLS + g_col] = vga_cell(c, g_attr);
    if (++g_col >= VGA_COLS) {
        g_col = 0;
        if (++g_row >= VGA_ROWS) {
            g_row = VGA_ROWS - 1;
        }
    }
    serial_putc(c);
}

void k_print(const char *s)
{
    while (*s != '\0') {
        k_putc(*s++);
    }
}

void k_backspace(void)
{
    if (g_col == 0 && g_row == 0) {
        return;
    }
    if (g_col > 0) {
        g_col--;
    } else {
        g_row--;
        g_col = VGA_COLS - 1;
    }
    VGA_MEM[g_row * VGA_COLS + g_col] = vga_cell(' ', g_attr);
    serial_putc('\b');
    serial_putc(' ');
    serial_putc('\b');
}

void k_print_hex64(uint64_t v)
{
    static const char HEX[] = "0123456789ABCDEF";
    k_putc('0');
    k_putc('x');
    for (int i = 60; i >= 0; i -= 4) {
        k_putc(HEX[(v >> i) & 0xF]);
    }
}
