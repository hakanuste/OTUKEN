#ifndef OTUKENOS_NVME_H
#define OTUKENOS_NVME_H

#include <stdint.h>

/* NVMe (Non-Volatile Memory Express) hazirligi.
 * Bu asamada: denetleyiciyi PCI uzerinden bulur, MMIO tabanini
 * haritalar, yetenek/surum/durum kayitlarini okur. TAM veri okuma
 * (admin/IO kuyruk cifti kurulumu, PRP listeleri) sonraki asamaya
 * birakildi. */

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

/* --- Gercek veri aktarimi (isim alani 1, 512 baytlik sektor gorunumu) ---
 * nvme_hazir: kuyruklar kuruldu ve isim alani kullanilabilir.
 * oku/yaz: en fazla 8 sektor/cagri; basari 0. yaz sonunda FLUSH verir. */
int      nvme_hazir(void);
uint64_t nvme_sektor_sayisi(void);
int      nvme_oku(uint64_t lba, uint32_t sayi, void *tampon);
int      nvme_yaz(uint64_t lba, uint32_t sayi, const void *tampon);

#endif
