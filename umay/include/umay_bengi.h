/* =====================================================================
 * BENGİ — kalıcı hafıza. Bengi taşa kazınan dört tür kayıt:
 *   KULLANICI : Hakan'a dair kalıcı bilgi
 *   PROJE     : proje durumu, aşama, açık iş
 *   KARAR     : verilmiş ve geri dönülmeyecek kararlar
 *   SONUC     : doğrulanmış görev çıktıları
 * Disk biçimi:  tur|dogrulandi|anahtar|deger
 * ===================================================================== */
#ifndef UMAY_BENGI_H
#define UMAY_BENGI_H
#include "umay.h"

typedef enum {
    BENGI_KULLANICI = 0,
    BENGI_PROJE,
    BENGI_KARAR,
    BENGI_SONUC,
    BENGI_TUR_SAYI
} bengi_tur_t;

typedef struct {
    bengi_tur_t tur;
    char        anahtar[32];
    char        deger[192];
    int         dogrulandi;
    uint64_t    damga;
    int         dolu;
} bengi_kayit_t;

int  bengi_kur(const char *yol);
int  bengi_kaydet(bengi_tur_t tur, const char *anahtar, const char *deger, int dogrulandi);
int  bengi_yaz(const char *anahtar, const char *deger);     /* = KULLANICI türü */
const char *bengi_oku(const char *anahtar);
int  bengi_dogrula(const char *anahtar);                    /* kaydı doğrulanmış yap */
int  bengi_sil(const char *anahtar);
int  bengi_ara(const char *kelime, const char **cikan, int en_cok);
int  bengi_turu_ara(bengi_tur_t tur, const bengi_kayit_t **cikan, int en_cok);
void bengi_ozet(char *c, uint32_t n);
void bengi_baglam(char *c, uint32_t n);        /* US'e verilecek hafıza özeti */
int  bengi_sakla(void);
int  bengi_sayi(void);
const char *bengi_tur_adi(bengi_tur_t t);
#endif
