/* Bağımsız dize/bellek araçları. libc varsayılmaz. */
#ifndef UMAY_STR_H
#define UMAY_STR_H
#include "umay.h"

void  um_bellek_kopya(void *h, const void *k, uint64_t n);
void  um_bellek_doldur(void *h, uint8_t d, uint64_t n);
uint32_t um_uzun(const char *s);
int   um_esit(const char *a, const char *b);
int   um_esit_n(const char *a, const char *b, uint32_t n);
int   um_bas_mi(const char *s, const char *on);
const char *um_icinde(const char *saman, const char *igne);
uint32_t um_kopya(char *h, uint32_t n, const char *k);      /* NUL'lu, taşmasız */
uint32_t um_ekle(char *h, uint32_t n, const char *k);
uint32_t um_sayi_yaz(char *h, uint32_t n, int64_t d);
/* Mini biçimleyici: %s %d %u %c %% */
uint32_t um_bicim(char *h, uint32_t n, const char *kalip, ...);

/* UTF-8 + Türkçe küçük harf (zeka.c mantığı korundu) */
uint32_t um_utf8_oku(const char *s, uint32_t *bayt);
uint32_t um_utf8_yaz(uint32_t kn, char *c);
uint32_t um_kucuk(uint32_t kn);
int      um_kelime_karakteri(uint32_t kn);
/* Doğru Türkçe küçültme (I->ı bayt uzunluğunu değiştirir, bu yüzden dışa yazar) */
uint32_t um_kucult(char *hedef, uint32_t n, const char *kaynak);
/* Bayt uzunluğunu koruyan küçültme — konum (offset) eşlemesi bozulmasın diye */
void     um_kucult_esbicim(char *s);
#endif
