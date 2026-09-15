#ifndef GDT_H
#define GDT_H

/* DEV-OS · GDT propia 64 bits: null + código kernel + datos kernel.
 * Sin TSS por ahora (se añadirá con ring3). */
void gdt_init(void);

#endif /* GDT_H */
