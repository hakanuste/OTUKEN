#ifndef OTUKENOS_PMM_H
#define OTUKENOS_PMM_H

#include <stdint.h>
#include "multiboot.h"

#define PMM_FRAME_SIZE 4096

void pmm_init(multiboot_info_t *mbi);

/* Bos bir 4KB fiziksel cerceve ayirir (fiziksel adresini dondurur),
 * bulamazsa 0 doner. Ayrilan cerceve icerigi sifirlanmaz (bu PMM'in
 * isi degil - VMM/kullanan katman gerekirse sifirlar). */
uint64_t pmm_alloc_frame(void);

/* Daha once pmm_alloc_frame ile alinmis bir cerceveyi serbest birakir. */
void pmm_free_frame(uint64_t phys_addr);

uint64_t pmm_total_frames(void);
uint64_t pmm_used_frames(void);
uint64_t pmm_free_frames(void);
uint64_t pmm_total_bytes(void);

#endif
