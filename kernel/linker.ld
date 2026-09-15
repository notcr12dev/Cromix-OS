/* ─────────────────────────────────────────────────────────────
 * DEV-OS · linker script del kernel (ELF64 -> bin plano).
 * Base física: 0x100000 (1 MB). stage2 copia ahí kernel.bin
 * y salta a _start (definido en kernel/entry.asm).
 * Salidas: build/kernel.elf (debug) y build/kernel.bin (plano).
 * ───────────────────────────────────────────────────────────── */
OUTPUT_FORMAT("elf64-x86-64");
OUTPUT_ARCH(i386:x86-64);
ENTRY(_start);

BASE = 0x100000;

SECTIONS
{
    . = BASE;

    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    }

    /* Símbolos útiles para depurar desde Python/QEMU si hace falta. */
    kernel_end = .;
}
