/* Cronix OS · idt.c: 256 gates, remapped + masked PIC.
 * Exceptions print name and registers to VGA + serial. */
#include "idt.h"

#include "io.h"
#include "print.h"
#include "vga.h"

#define IDT_COUNT 256
#define GATE_ATTR 0x8EU /* presente, DPL0, puerta interrupción 64 */

struct idt_entry {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t ist;
    uint8_t attr;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void idt_load(uint64_t ptr);
extern void (*irq_table[224])(void);

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

static struct idt_entry g_idt[IDT_COUNT];

static const char *const EXC_NAMES[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "BOUND Range", "Invalid Opcode", "No FPU",
    "Double Fault", "Coprocessor Overrun", "Invalid TSS", "Segment Missing",
    "Stack Fault", "General Protection", "Page Fault", "Reserved(15)",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD Error",
    "Virtualization", "Control Protection", "Reserved(22)", "Reserved(23)",
    "Reserved(24)", "Reserved(25)", "Reserved(26)", "Reserved(27)",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved(31)"
};

static void set_gate(int n, void (*fn)(void))
{
    uint64_t addr = (uint64_t)fn;
    g_idt[n].off_lo = (uint16_t)(addr & 0xFFFF);
    g_idt[n].sel = 0x08;
    g_idt[n].ist = 0;
    g_idt[n].attr = GATE_ATTR;
    g_idt[n].off_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    g_idt[n].off_hi = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    g_idt[n].zero = 0;
}

/* 8259 PIC: remap IRQ0-15 to int 0x20-0x2F, mask everything.
 * Without this, IRQ0 lands on vector 8 (#Double Fault). */
static void pic_remap(void)
{
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20); /* PIC1 base = 0x20 */
    io_wait();
    outb(0xA1, 0x28); /* PIC2 base = 0x28 */
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();
    outb(0x21, 0xFF); /* full mask: shell polls */
    outb(0xA1, 0xFF);
}

void idt_init(void)
{
    static void (*const isrs[32])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };

    for (int i = 0; i < 32; i++) {
        set_gate(i, isrs[i]);
    }
    for (int i = 32; i < IDT_COUNT; i++) {
        set_gate(i, irq_table[i - 32]);
    }

    struct idt_ptr p;
    p.limit = (uint16_t)(sizeof(g_idt) - 1);
    p.base = (uint64_t)&g_idt;
    idt_load((uint64_t)&p);
    pic_remap();
}

void isr_handler(struct isr_frame *f)
{
    k_color(VGA_LIGHT_RED, VGA_BLACK);
    k_print("\n*** CPU EXCEPTION ");
    if (f->int_no < 32) {
        k_print(EXC_NAMES[f->int_no]);
    } else {
        k_print("unknown");
    }
    k_print(" ***\n  int=");
    k_print_hex64(f->int_no);
    k_print(" err=");
    k_print_hex64(f->err_code);
    k_print("\n  rip=");
    k_print_hex64(f->rip);
    k_print(" cs=");
    k_print_hex64(f->cs);
    k_print("\n  rflags=");
    k_print_hex64(f->rflags);
    k_print(" rsp=");
    k_print_hex64(f->rsp);
    k_print("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void irq_handler(struct isr_frame *f)
{
    if (f->int_no >= 40) {
        outb(0xA0, 0x20); /* slave EOI */
    }
    outb(0x20, 0x20); /* master EOI */
}
