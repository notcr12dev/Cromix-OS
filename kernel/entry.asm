; ─────────────────────────────────────────────────────────────
; DEV-OS · kernel entry (64 bits, ELF64)
; El bootloader salta a _start (físico 0x100000).
; Pone una pila propia y llama a kmain(). Si kmain
; retorna, apaga interrupciones y se queda en hlt.
; Ensamblar: nasm -f elf64 kernel/entry.asm -o build/entry.o
; ─────────────────────────────────────────────────────────────
BITS 64
SECTION .text
GLOBAL _start
EXTERN kmain

_start:
    cli
    mov rsp, stack_top   ; pila definida abajo (16 KB)
    mov rbp, rsp
    and rsp, -16         ; alinear a 16 B (ABI System V)
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
