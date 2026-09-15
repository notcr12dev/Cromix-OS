/* ─────────────────────────────────────────────────────────────
 * Cronix OS · kmain: init print, own GDT, own IDT, shell.
 * Order matters: print -> gdt -> idt (+PIC mask) -> sti -> shell.
 * ───────────────────────────────────────────────────────────── */
#include "gdt.h"
#include "idt.h"
#include "print.h"
#include "shell.h"
#include "vfs.h"
#include "vga.h"

void kmain(void)
{
    print_init();
    k_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    gdt_init();
    idt_init();
    __asm__ volatile("sti"); /* IRQs masked in PIC: safe */

    k_color(VGA_LIGHT_GREEN, VGA_BLACK);
    k_print("===============================================\n");
    k_print("  Cronix OS kernel x86_64 ... OK\n");
    k_print("===============================================\n");
    k_color(VGA_LIGHT_GREY, VGA_BLACK);
    k_print("boot: BIOS stage1+stage2 -> long mode -> 0x100000\n");
    k_print("cpu : x86_64, 2MB identity paging 0-16MB\n");
    k_print("desc: own GDT + IDT 0-31 + PIC 0x20/0x28 masked\n");
    k_print("io  : VGA 80x25 | COM1 38400 8N1 | PS/2 polling\n");
    if (vfs_mount() == 0) {
        k_print("disk: FAT16 16MB @LBA2048 (bin sys home)\n");
    } else {
        k_color(VGA_LIGHT_RED, VGA_BLACK);
        k_print("disk: MOUNT FAILED - storage offline\n");
        k_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
    k_print("\n");

    shell_run(); /* never returns */

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
