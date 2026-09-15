#ifndef IDT_H
#define IDT_H

/* DEV-OS · IDT propia 64 bits: vectores 0-31 (excepciones CPU)
 * con volcado, 32-255 a manejador IRQ genérico. PIC remapeado
 * a 0x20/0x28 y todo enmascarado (shell usa polling, sin IRQ). */
#include <stdint.h>

/* Orden exacto de push en cpu.asm:isr_common. */
struct isr_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

void idt_init(void);
void isr_handler(struct isr_frame *f); /* excepciones 0-31: vuelca y para */
void irq_handler(struct isr_frame *f); /* IRQ 32-255: solo EOI */

#endif /* IDT_H */
