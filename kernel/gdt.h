#ifndef GDT_H
#define GDT_H

/* Cronix OS · own 64-bit GDT: null + kernel code + kernel data.
 * No TSS yet (comes with ring3). */
void gdt_init(void);

#endif /* GDT_H */
