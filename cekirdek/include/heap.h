#ifndef OTUKENOS_HEAP_H
#define OTUKENOS_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(void);

void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);

/* kmalloc/kmalloc_aligned ile alinan bloklar SADECE kendi karsilik
 * gelen serbest birakma fonksiyonuyla serbest birakilmali - ikisi
 * birbirinin yerine kullanilamaz (farkli ic yapilari var). */
void kfree(void *ptr);
void kfree_aligned(void *ptr);

void *krealloc(void *ptr, size_t new_size);

void heap_get_stats(uint64_t *total_bytes, uint64_t *used_bytes,
                     uint64_t *free_bytes, uint64_t *block_count);

#endif
