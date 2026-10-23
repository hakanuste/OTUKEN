#ifndef OTUKENOS_UYGULAMA_H
#define OTUKENOS_UYGULAMA_H
#include <stdint.h>

/* 1.4.8 — Masaüstü uygulama yükleyicisi.
 * Her uygulama diskteki bir ELF'tir ve KENDİ adres alanında (ayrı süreç)
 * Ring3'te, yalnız kendi TÖRE profiliyle koşar. Tek kopya kuralı: uygulama
 * zaten açıksa yenisi başlatılmaz, penceresi öne getirilir. */
enum {
    UYG_SISTEM  = 0,     /* ÖTÜKEN: dizge izleyicisi (SISTEM.ELF)  */
    UYG_GORUNUM = 1,     /* ÜLGEN: tema seçici (GORUNUM.ELF)        */
    UYG_AGSINA  = 2,     /* yalnız TANI: ağ sınaması (AGSINA.ELF)   */
    UYG_TULPAR  = 3,     /* TULPAR: ağ gezgini (TULPAR.ELF)         */
    UYG_KERVAN  = 4,     /* KERVAN: paket yöneticisi (KERVAN.ELF)   */
    UYG_ULAK    = 5,     /* ULAK: ağ durumu (ULAK.ELF)              */
    UYG_DEDE    = 6,     /* DEDE KORKUT: söz ve ses (DEDE.ELF)      */
    UYG_TORE    = 7,     /* TÖRE: yetki/politika görüntüleyici      */
    UYG_TIMUR   = 8,     /* TİMUR: güvenlik korumaları              */
    UYG_KUT     = 9,     /* KUT: kimlik ve yetkilendirme            */
    UYG_ASENA   = 10,    /* ASENA: kurtarma (disk denetim/onarim)   */
    UYG_ERGENEKON = 11,  /* ERGENEKON: yeniden kurulum              */
    UYG_TAMGA   = 12,    /* TAMGA: dijital kimlik                   */
    UYG_SAYI
};

/* 1.4.9: KERVAN paketlerinin uygulamaları için yuvalar (UYG_SAYI ... +3) */
#define UYG_PAKET_YUVA 4
int uygulama_paket_ac(const char *dosya);   /* yuva (uygulama no) / -1 */

/* Başlatır ya da (açıksa) öne getirir.
 * Dönüş: 1 yeni süreç başladı, 0 zaten açıktı (öne getirildi/açılıyor),
 * -1 hata (süreç oluşturulamadı). gorsel_kilit TUTULURKEN de çağrılabilir. */
int uygulama_ac(int u);

/* Sınama/tanı: uygulamanın süreç kimliği (açık değilse 0) ve penceresi
 * (yoksa -1). */
uint32_t uygulama_pid(int u);
uint32_t uygulama_gorev(int u);
int      uygulama_penceresi(int u);
const char *uygulama_adi(int u);
/* Açılıştan beri bu uygulama için oluşturulan süreç sayısı (tek kopya
 * sınaması: başka görevlerin doğup ölmesinden etkilenmez). */
uint32_t uygulama_baslatma_sayisi(int u);

#endif
