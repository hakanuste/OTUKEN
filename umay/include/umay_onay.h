/* ONAY — yıkıcı işler için kullanıcı onayı (V9.2 onay_bekleyenler dengi). */
#ifndef UMAY_ONAY_H
#define UMAY_ONAY_H
#include "umay.h"

#define ONAY_MAX 8

typedef struct {
    char     kimlik[16];
    char     tur[24];
    char     baslik[96];
    char     ozet[256];
    char     is[UMAY_YOL_MAX];      /* yapılacak işin tanımı */
    uint64_t olustu;
    uint64_t son;                   /* geçerlilik bitişi (ms) */
    int      dolu;
} onay_t;

void  onay_kur(uint32_t omur_ms);
const char *onay_iste(const char *tur, const char *baslik,
                      const char *ozet, const char *is);   /* kimlik döner */
onay_t *onay_bul(const char *kimlik);
int   onay_ver(const char *kimlik);       /* 1 = onaylandı ve tüketildi */
int   onay_reddet(const char *kimlik);
void  onay_temizle(void);
int   onay_bekleyen_sayi(void);
onay_t *onay_sirali(int i);
#endif
