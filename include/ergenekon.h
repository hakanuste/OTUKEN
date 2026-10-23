#ifndef OTUKENOS_ERGENEKON_H
#define OTUKENOS_ERGENEKON_H
/* =====================================================================
 * 1.4.9 — ERGENEKON: yeniden kurulum.
 *
 * Dizge imajı /boot/ergenekon.krv GRUB modülü olarak belleğe gelir (CD
 * sürücüsü gerekmez). Biçim KERVAN v2 ile aynıdır ve AYNI anahtarlarla
 * imzalıdır: imza ve her dosyanın SHA-256'sı doğrulanmadan diske hiçbir
 * şey yazılmaz. İşlemler:
 *   yenile   : diskteki dizge dosyalarından eksik ya da imajdan farklı
 *              olanlar imajdan yazılır. Kullanıcı dosyaları ve KERVAN
 *              paketleri KALIR.
 *   sifirdan : birim biçimlenir (FAT ve kök dizin boşaltılır, birim adı
 *              korunur), imajdaki dosyalar yazılır. BÜTÜN kullanıcı
 *              dosyaları ve paketler GİDER.
 *   yeniden başlat: 8042 denetleyicisi ile.
 * Sınır: alt dizinler (FAT16'da) biçimlemede gider; imaj yalnız kök
 * dizindeki dosyaları taşır.
 * ===================================================================== */
#include <stdint.h>

/* Ring3 ile aynı düzen (128 bayt) */
typedef struct {
    uint8_t  var, gecerli, anahtar_no, b;
    uint32_t dosya, boy;
    uint32_t yazilan, ayni, hata;     /* son işlemin sonucu */
    char     ad[24];
    char     surum[16];
    char     neden[64];
} ergenekon_durum_t;
_Static_assert(sizeof(ergenekon_durum_t) == 128, "ergenekon durum");

/* dizge dosyasının durumu (ASENA'nın dizge denetimi) */
typedef struct {
    char     ad[13];
    uint8_t  durum;                   /* 0 ayni, 1 farkli, 2 diskte yok */
    uint16_t b;
    uint32_t boy;                     /* imajdaki boy */
    uint32_t disk_boy;
} ergenekon_dosya_t;
_Static_assert(sizeof(ergenekon_dosya_t) == 24, "ergenekon dosya");

int  ergenekon_durum(ergenekon_durum_t *d);                 /* imaj bilgisi; 0 / -1 */
/* dizge dosyalarını imajla karşılaştır: sira==0 yeniden hesaplar */
int  ergenekon_dosya(int sira, ergenekon_dosya_t *o);
int  ergenekon_dosya_kurtar(const char *ad, char *neden);   /* tek dosyayı imajdan yaz */
int  ergenekon_yenile(ergenekon_durum_t *d);
int  ergenekon_sifirdan(ergenekon_durum_t *d);
void ergenekon_yeniden_baslat(void) __attribute__((noreturn));
/* imajdaki dosya mı (KERVAN koruması için) */
int  ergenekon_imaj_dosyasi_mi(const char *ad);
#endif
