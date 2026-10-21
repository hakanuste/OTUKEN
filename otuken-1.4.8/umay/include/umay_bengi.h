/* =====================================================================
 * BENGİ — kalıcı hafıza. Bengi taşa kazınan dört tür kayıt:
 *   KULLANICI : Hakan'a dair kalıcı bilgi
 *   PROJE     : proje durumu, aşama, açık iş
 *   KARAR     : verilmiş ve geri dönülmeyecek kararlar
 *   SONUC     : doğrulanmış görev çıktıları
 * Disk biçimi:  tur|dogrulandi|anahtar|deger
 *   (denetim karakteri hiçbir alana, '|' anahtara giremez: bengi_kaydet
 *    bunları REDDEDER; çok satırlı metin önce bengi_duzle ile düzlenir)
 *
 * KONUŞMADAN BELLEĞE YOL (hiçbir adım atlanmaz):
 *   konuşma -> ADAY (bengi_denetle: buyruk sözcükleri atılır; boş, soru,
 *   çok uzun, denetim karakterli, yinelenen söz reddedilir)
 *   -> kullanıcı ONAYI (YAZMAN işi GT_UYGULA'dır: onaysız koşmaz)
 *   -> DOĞRULANMIŞ kayıt -> disk (bengi_sakla).
 *   Onaysız yazılan kayıt ADAY kalır (dogrulandi=0) ve US bağlamına girmez.
 *   Görev çıktıları (SONUC) her zaman ADAY girer; "doğrula <anahtar>"
 *   (onaylı YAZMAN işi) ile doğrulanır. SONUC hiçbir zaman US bağlamına
 *   girmez (bengi_baglam).
 * MODEL AĞIRLIKLARI: BENGİ hiçbir yoldan US1 modeline ya da eğitim
 *   derlemine yazmaz; model yalnız diskteki UMAY.US1 dosyasından OKUNUR.
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
int  bengi_dogrulandi_mi(const char *anahtar);
/* Konuşmadan gelen sözü hafıza adayına çevirir (yukarıya bak). UM_OLDU:
 * temiz[] yazılabilir söz; değilse neden[] reddin gerekçesi. */
int  bengi_denetle(const char *ham, char *temiz, uint32_t n, char *neden, uint32_t nn);
/* Denetim karakterlerini (satır sonu dahil) tek boşluğa indirir. */
void bengi_duzle(const char *ham, char *c, uint32_t n);
int  bengi_sil(const char *anahtar);
int  bengi_ara(const char *kelime, const char **cikan, int en_cok);
int  bengi_turu_ara(bengi_tur_t tur, const bengi_kayit_t **cikan, int en_cok);
void bengi_ozet(char *c, uint32_t n);
void bengi_baglam(char *c, uint32_t n);        /* US'e verilecek hafıza özeti */
int  bengi_sakla(void);
int  bengi_sayi(void);
const char *bengi_tur_adi(bengi_tur_t t);
#endif
