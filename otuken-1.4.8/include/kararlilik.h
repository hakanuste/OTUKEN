#ifndef OTUKENOS_KARARLILIK_H
#define OTUKENOS_KARARLILIK_H
#include <stdint.h>

/* --- Asama 41: Kararlilik izleme ve baski sinamasi ---
 * Uzun sureli calismada sizinti, tukenme ve bozulmayi YAKALAMAK icin
 * cekirdek kaynaklarini periyodik olarak orneklendirir. */

typedef struct {
    uint64_t ilk_bos_cerceve;
    uint64_t son_bos_cerceve;
    uint64_t ilk_heap_bos;
    uint64_t son_heap_bos;
    uint32_t ornek_sayisi;
    uint32_t gorev_sayisi;
    uint64_t calisma_tik;
    int      sizinti_supheli;
} kararlilik_t;

void kararlilik_baslat(void);
void kararlilik_ornekle(void);        /* periyodik cagrilir */
const kararlilik_t *kararlilik_al(void);
void kararlilik_rapor(void);

/* Baski sinamasi: yogun ayirma/iade + gorev olusturma/sonlandirma.
 * Donus: tespit edilen hata sayisi (0 = temiz). */
int  kararlilik_baski_sinamasi(int tur_sayisi);

#endif
