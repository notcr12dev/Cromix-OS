; ─────────────────────────────────────────────────────────────
; Cronix OS · cpu.asm (64-bit, ELF64)
; gdt_flush / idt_load + ISR stubs 0-31 + IRQ stubs 32-255.
; Vectors with CPU error code: 8,10,11,12,13,14,17,21,29,30.
; Rest push a dummy 0. Full frame in cpu.asm = struct
; isr_frame in idt.h (same push order).
; Build: nasm -f elf64 kernel/cpu.asm -o build/cpu.o
; ─────────────────────────────────────────────────────────────
BITS 64
SECTION .text

GLOBAL gdt_flush
GLOBAL idt_load
EXTERN isr_handler
EXTERN irq_handler

; void gdt_flush(uint64_t ptr): lgdt + reload CS via retfq.
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    push 0x08
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    ret

; void idt_load(uint64_t ptr): lidt.
idt_load:
    lidt [rdi]
    ret

; ── stubs excepciones ─────────────────────────────────────────
%macro ISR_NOERR 1
GLOBAL isr%1
isr%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
GLOBAL isr%1
isr%1:
    push %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

; Común: guarda regs, RDI = frame, llama C, restaura, iretq.
isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call isr_handler
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                ; int_no + err_code
    iretq

; ── stubs IRQ 32-255 (generados) ──────────────────────────────
%assign v 32
%rep 224
GLOBAL irq %+ v
irq %+ v:
    push 0
    mov eax, v               ; mov r32 zero-extiende: vector exacto 32-255
    push rax
    jmp irq_common
%assign v v+1
%endrep

irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call irq_handler
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

; Tabla de puntos de entrada IRQ para idt.c.
SECTION .data
GLOBAL irq_table
irq_table:
%assign v 32
%rep 224
    dq irq %+ v
%assign v v+1
%endrep
