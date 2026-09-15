/* ─────────────────────────────────────────────────────────────
 * DEV-OS · kmain: init print, GDT propia, IDT propia, shell.
 * Orden importa: print -> gdt -> idt (+PIC mask) -> sti -> shell.
 * ───────────────────────────────────────────────────────────── */
#include "gdt.h"
#include "idt.h"
#include "print.h"
#include "shell.h"
#include "vga.h"

void kmain(void)
{
    print_init();
    k_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    gdt_init();
    idt_init();
    __asm__ volatile("sti"); /* IRQs enmascarados en PIC: seguro */

    k_color(VGA_LIGHT_GREEN, VGA_BLACK);
    k_print("===============================================\n");
    k_print("  Cronix-OS kernel x86_64 ... OK\n");
    k_print("===============================================\n");
    k_color(VGA_LIGHT_GREY, VGA_BLACK);
    k_print("boot: BIOS stage1+stage2 -> long mode -> 0x100000\n");
    k_print("cpu : x86_64, paginacion 2MB identity 0-4MB\n");
    k_print("desc: GDT propia + IDT 0-31 + PIC 0x20/0x28 mask\n");
    k_print("io  : VGA 80x25 | COM1 38400 8N1 | PS/2 polling\n\n");

    shell_run(); /* no retorna */

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
