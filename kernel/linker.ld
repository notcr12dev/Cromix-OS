/* ─────────────────────────────────────────────────────────────
 * Cronix OS · kernel linker script (ELF64 -> flat binary).
 * Physical base: 0x100000 (1 MB). stage2 copies kernel.bin
 * there and jumps to _start (defined in kernel/entry.asm).
 * Outputs: build/kernel.elf (debug) and build/kernel.bin (flat).
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

    /* Handy symbols for Python/QEMU debugging. */
    kernel_end = .;
}
