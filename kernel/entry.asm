; ─────────────────────────────────────────────────────────────
; Cronix OS · kernel entry (64-bit, ELF64)
; Bootloader jumps to _start (physical 0x100000).
; Sets up its own stack and calls kmain(). If kmain
; returns, disables interrupts and halts.
; Build: nasm -f elf64 kernel/entry.asm -o build/entry.o
; ─────────────────────────────────────────────────────────────
BITS 64
SECTION .text
GLOBAL _start
EXTERN kmain

_start:
    cli
    mov rsp, stack_top   ; stack defined below (16 KB)
    mov rbp, rsp
    and rsp, -16         ; align to 16 B (System V ABI)
    call kmain
.hang:
    cli
    hlt
    jmp .hang

SECTION .bss
ALIGN 16
stack_bottom:
    resb 16384            ; 16 KB
stack_top:
