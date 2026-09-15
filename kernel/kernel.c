/* ─────────────────────────────────────────────────────────────
 * DEV-OS · kernel mínimo x86_64 (freestanding, sin libc).
 * Arranca en 64 bits desde stage2 (salto a 0x100000/_start).
 * Hace: limpiar VGA, banner, serie COM1, halt.
 *
 * Base para el futuro SO enfocado en desarrollo:
 * aquí se añadirán IDT/GDT, gestión de memoria, syscalls, etc.
 * Estilo: C11, funciones pequeñas, sin globals mutables salvo
 * cursor VGA y puerto serie.
 * ───────────────────────────────────────────────────────────── */
#include <stddef.h>
#include <stdint.h>

#include "vga.h"

/* ── Serie COM1 (0x3F8) vía outb/inb ────────────────────────── */
#define COM1 0x3F8u

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

static void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* IRQ off */
    outb(COM1 + 3, 0x80); /* DLAB on */
    outb(COM1 + 0, 0x03); /* 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFO */
    outb(COM1 + 4, 0x0B); /* RTS+DTR */
}

static int serial_ready(void)
{
    return (inb(COM1 + 5) & 0x20) != 0;
}

static void serial_putc(char c)
{
    while (!serial_ready()) {
        /* espera activa */
    }
    outb(COM1, (uint8_t)c);
}

/* ── VGA ────────────────────────────────────────────────────── */
static size_t g_row;
static size_t g_col;
static uint8_t g_attr;

static void vga_clear(void)
{
    for (size_t y = 0; y < VGA_ROWS; y++) {
        for (size_t x = 0; x < VGA_COLS; x++) {
            VGA_MEM[y * VGA_COLS + x] = vga_cell(' ', g_attr);
        }
    }
    g_row = 0;
    g_col = 0;
}

static void vga_putc(char c)
{
    if (c == '\n') {
        g_col = 0;
        if (++g_row >= VGA_ROWS) {
            g_row = VGA_ROWS - 1; /* sin scroll: se queda abajo */
        }
        return;
    }
    VGA_MEM[g_row * VGA_COLS + g_col] = vga_cell(c, g_attr);
    if (++g_col >= VGA_COLS) {
        g_col = 0;
        if (++g_row >= VGA_ROWS) {
            g_row = VGA_ROWS - 1;
        }
    }
}

static size_t k_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

/* Escribe en VGA + serie a la vez (la serie sirve para logs/QEMU). */
static void k_print(const char *s)
{
    for (size_t i = 0, n = k_strlen(s); i < n; i++) {
        char c = s[i];
        vga_putc(c);
        if (c == '\n') {
            serial_putc('\r');
        }
        serial_putc(c);
    }
}

/* ── Entrada del kernel (la llama entry.asm:_start) ─────────── */
void kmain(void)
{
    g_attr = vga_attr(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
    serial_init();

    g_attr = vga_attr(VGA_LIGHT_GREEN, VGA_BLACK);
    k_print("===============================================\n");
    k_print("  Cronix kernel x86_64 ... OK\n");
    k_print("===============================================\n");
    g_attr = vga_attr(VGA_LIGHT_GREY, VGA_BLACK);
    k_print("boot: BIOS stage1+stage2 -> long mode -> 0x100000\n");
    k_print("cpu : x86_64, paginacion 2MB identity 0-4MB\n");
    k_print("vga : 80x25 texto | serie: COM1 38400 8N1\n");
    k_print("\nTODO siguiente: IDT, GDT propia, pmm/kheap, shell.\n");
    k_print("Sistema detenido (hlt). Reinicia con el boton de QEMU.\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
