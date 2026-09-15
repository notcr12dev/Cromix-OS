#ifndef HEAP_H
#define HEAP_H

/* Cronix OS · kernel heap: first-fit kmalloc/kfree over a fixed
 * 4MB pool at 0x400000 (mapped by stage2: identity 0-16MB).
 * No sbrk yet: pool is fixed, kmalloc returns NULL when full. */
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t n);
void kfree(void *p);

#endif /* HEAP_H */
