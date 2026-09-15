/* DEV-OS · shell.c: teclado PS/2 por polling + línea de comandos.
 * Set 1 make-codes; shift izq/der; ignora extendidos (0xE0) y releases
 * salvo shift. eco sale por VGA y serie vía print.c. */
#include "shell.h"

#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "print.h"
#include "vga.h"

#define KBD_STATUS 0x64u
#define KBD_DATA 0x60u
#define LINE_MAX 128

/* Mapa set-1 make -> ASCII (0 = sin tecla imprimible). */
static const char KMAP[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0',     [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char KMAP_SHIFT[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

static int k_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Bloquea hasta scancode listo; devuelve byte crudo. */
static uint8_t kbd_read(void)
{
    while ((inb(KBD_STATUS) & 0x01) == 0) {
        __asm__ volatile("pause");
    }
    return inb(KBD_DATA);
}

/* Siguiente evento de tecla: -1 = ignorar, '\n' = enter,
 * '\b' = borrar, resto = char listo. */
static int kbd_next(int *shift)
{
    uint8_t c = kbd_read();
    if (c == 0xE0) { /* extendido: consume siguiente y olvida */
        kbd_read();
        return -1;
    }
    if (c == 0x2A || c == 0x36) {
        *shift = 1;
        return -1;
    }
    if (c == 0xAA || c == 0xB6) {
        *shift = 0;
        return -1;
    }
    if (c & 0x80) { /* release: ignora */
        return -1;
    }
    if (c == 0x1C) {
        return '\n';
    }
    if (c == 0x0E) {
        return '\b';
    }
    if (c >= 128) {
        return -1;
    }
    char ch = (*shift ? KMAP_SHIFT[c] : KMAP[c]);
    return ch == 0 ? -1 : (int)ch;
}

static void cmd_help(void)
{
    k_print("comandos: help | info | clear | halt | reboot\n");
}

static void cmd_info(void)
{
    k_print("Cronix-OS x86_64 | gdt propia | idt 0-31 | pic 0x20/0x28 mask\n");
    k_print("mem: ident 2MB 0-4MB | kernel @0x100000 | pila 16KB\n");
}

static void cmd_exec(const char *line)
{
    if (*line == '\0') {
        return;
    }
    if (k_eq(line, "help")) {
        cmd_help();
    } else if (k_eq(line, "info")) {
        cmd_info();
    } else if (k_eq(line, "clear")) {
        vga_clear();
    } else if (k_eq(line, "halt")) {
        k_print("halted.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    } else if (k_eq(line, "reboot")) {
        k_print("reboot...\n");
        outb(0x64, 0xFE); /* pulso reset vía 8042 */
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    } else {
        k_print("shell: comando '?': ");
        k_print(line);
        k_print(" (prueba help)\n");
    }
}

void shell_run(void)
{
    char line[LINE_MAX];
    size_t len = 0;
    int shift = 0;

    k_print("shell lista. Escribe help + ENTER.\n");
    for (;;) {
        k_color(VGA_LIGHT_GREEN, VGA_BLACK);
        k_print("cronix-os> ");
        k_color(VGA_LIGHT_GREY, VGA_BLACK);
        len = 0;
        for (;;) {
            int ev = kbd_next(&shift);
            if (ev < 0) {
                continue;
            }
            if (ev == '\n') {
                line[len] = '\0';
                k_putc('\n');
                break;
            }
            if (ev == '\b') {
                if (len > 0) {
                    len--;
                    k_backspace();
                }
                continue;
            }
            if (len + 1 < LINE_MAX) {
                line[len++] = (char)ev;
                k_putc((char)ev);
            }
        }
        cmd_exec(line);
    }
}
