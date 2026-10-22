/* ORTAK — ayarlar, günlük, zaman, açılış (V9.2 ortak.c dengi, cJSON'suz). */
#ifndef UMAY_ORTAK_H
#define UMAY_ORTAK_H
#include "umay.h"

#define AYAR_MAX 32

void        ayar_kur(const char *yol);
const char *ayar_metin(const char *anahtar, const char *varsayilan);
int64_t     ayar_sayi(const char *anahtar, int64_t varsayilan);
int         ayar_yaz(const char *anahtar, const char *deger);
int         ayar_sakla(void);

void        gun_yaz(const char *bicim, ...);      /* günlük satırı */
void        gun_yolu_ayarla(const char *yol);

/* "barış/savaş" yerine ÖTÜKEN'de: dinginlik/tetikte kipi */
typedef enum { KIP_DINGIN = 0, KIP_TETIKTE = 1 } kip_t;
kip_t       umay_kip(void);
void        umay_kip_ayarla(kip_t k);
#endif
