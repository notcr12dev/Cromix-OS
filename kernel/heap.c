/* Cronix OS · heap.c: free-list allocator, 16-byte aligned.
 * Single static pool, split on alloc, coalesce on free. */
#include "heap.h"

#include <stdint.h>

#define HEAP_START ((uintptr_t)0x400000)
#define HEAP_SIZE  ((size_t)(4 * 1024 * 1024))

struct block {
    size_t size;          /* usable bytes after this header */
    int free;
    struct block *next;
};

static struct block *g_head = (struct block *)0;

void heap_init(void)
{
    g_head = (struct block *)HEAP_START;
    g_head->size = HEAP_SIZE - sizeof(struct block);
    g_head->free = 1;
    g_head->next = (struct block *)0;
}

static size_t align16(size_t n)
{
    return (n + 15) & ~(size_t)15;
}

void *kmalloc(size_t n)
{
    size_t need;
    struct block *b;

    if (n == 0) {
        n = 1;
    }
    need = align16(n);
    for (b = g_head; b != (struct block *)0; b = b->next) {
        if (b->free && b->size >= need) {
            /* Split if the leftover fits a new header + 16 bytes. */
            if (b->size >= need + sizeof(struct block) + 16) {
                struct block *nb =
                    (struct block *)((uintptr_t)(b + 1) + need);
                nb->size = b->size - need - sizeof(struct block);
                nb->free = 1;
                nb->next = b->next;
                b->size = need;
                b->next = nb;
            }
            b->free = 0;
            return (void *)(b + 1);
        }
    }
    return (void *)0; /* pool exhausted */
}

void kfree(void *p)
{
    struct block *b;
    struct block *cur;

    if (p == (void *)0) {
        return;
    }
    b = ((struct block *)p) - 1;
    b->free = 1;
    /* Coalesce forward in one pass. */
    for (cur = g_head; cur != (struct block *)0; cur = cur->next) {
        while (cur->free && cur->next != (struct block *)0 &&
               cur->next->free) {
            cur->size += sizeof(struct block) + cur->next->size;
            cur->next = cur->next->next;
        }
    }
}
