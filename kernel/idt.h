#ifndef IDT_H
#define IDT_H

/* Cronix OS · own 64-bit IDT: vectors 0-31 (CPU exceptions)
 * with register dump, 32-255 to a generic IRQ handler. PIC
 * remapped to 0x20/0x28, fully masked (shell polls, no IRQ). */
#include <stdint.h>

/* Exact push order in cpu.asm:isr_common. */
struct isr_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

void idt_init(void);
void isr_handler(struct isr_frame *f); /* exceptions 0-31: dump and halt */
void irq_handler(struct isr_frame *f); /* IRQ 32-255: EOI only */

#endif /* IDT_H */
