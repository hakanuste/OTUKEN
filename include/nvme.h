#ifndef OTUKENOS_NVME_H
#define OTUKENOS_NVME_H

#include <stdint.h>

/* NVMe (Non-Volatile Memory Express) hazirligi.
 * Denetleyiciyi bulur, sifirlar, yonetim ve G/C kuyruk ciftlerini kurar
 * (bkz. src/nvme.c). */

typedef struct {
    int      present;
    uint64_t mmio_base;
    uint64_t capabilities;   /* CAP - 64 bit */
    uint32_t version;        /* VS */
    uint32_t controller_config;   /* CC */
    uint32_t controller_status;   /* CSTS */
    uint32_t max_queue_entries;
    uint32_t doorbell_stride;
    int      ready;          /* CSTS.RDY biti */
} nvme_controller_t;

int nvme_init(void);
const nvme_controller_t *nvme_get_controller(void);

/* 1.4.9: veri yolu. Ad alani 1, 512 baytlik sektor, istek basina en cok 8
 * sektor. 0 basari, -1 hata. Yazma FLUSH ile biter. */
int      nvme_hazir(void);
uint64_t nvme_sektor_sayisi(void);
int      nvme_oku(uint64_t lba, uint32_t sayi, void *tampon);
int      nvme_yaz(uint64_t lba, uint32_t sayi, const void *tampon);

#endif
