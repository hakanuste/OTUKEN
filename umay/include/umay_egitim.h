/* EĞİTİM (1.5) — UMAY kendi US1 modelini boşta, kendi kütüphanesiyle eğitir.
 * Ayrıntı: us/umay_egitim.c. */
#ifndef UMAY_EGITIM_H
#define UMAY_EGITIM_H
#include "umay.h"
void egitim_kur(void);          /* açılışta (ayarlar, önceki eğitimin imi) */
void egitim_pompa(void);        /* her UMAY turunda: en çok bir dizi ileri+geri */
void egitim_etkinlik(void);     /* ileti geldi: boşta sayacı sıfırlanır */
int  egitim_suruyor(void);
#endif
