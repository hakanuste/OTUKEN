/* ORBITRON — ÜLGEN arayüz yazı tipi. SIL OFL 1.1.
 * 18 satır, oranlı genişlik, 4 bit örtme (kenar yumuşatmalı).
 * Çekirdeğin 8x8 yazı tipi terminal/günlük için yerinde kalır;
 * bu yazı tipi masaüstü, rıhtım ve üst bar gibi arayüz yüzeyleri içindir. */
#ifndef OTUKENOS_ORBITRON_H
#define OTUKENOS_ORBITRON_H

#include <stdint.h>
#include "fb.h"

typedef struct {
    uint32_t kod;
    uint8_t  en;
    uint8_t  ilerle;
    uint32_t ofset;
} orb_glif_t;

/* kucuk: 0 = 13 piksel (baslik), 1 = 10 piksel (etiket) */
int  orb_satir_yuksekligi(int kucuk);
int  orb_en(const char *s, int kucuk);
void orb_yaz(int x, int y, const char *s, uint32_t renk, int golge, int kucuk);
void orb_yaz_ortali(int orta_x, int y, const char *s, uint32_t renk, int golge, int kucuk);
/* Azami genisligi asmayacak sekilde kirpar ve ortalar. */
void orb_yaz_ortali_sinirli(int orta_x, int y, const char *s, uint32_t renk,
                            int golge, int kucuk, int azami_en);
/* En cok iki satira sarar (bosluktan boler), ortalar. Satir sayisini doner. */
int  orb_yaz_ortali_sar(int orta_x, int y, const char *s, uint32_t renk,
                        int golge, int kucuk, int azami_en, int satir_ara);
#endif
