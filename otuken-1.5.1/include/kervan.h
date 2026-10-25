#ifndef OTUKENOS_KERVAN_H
#define OTUKENOS_KERVAN_H

#include <stdint.h>

/* --- Asama 37: KERVAN Paket Sistemi ---
 * ÖTÜKEN uygulama paketleri icin basit, dogrulanabilir bir bicim.
 * Paket = basik (header) + girdi tablosu + veri bloklari.
 * Disk uzerinde BITIG (FAT16) dosyasi olarak durur.
 */

#define KERVAN_SIHIR      0x4E41565245934BULL  /* rastgele degil: "K.ERVAN" */
#define KERVAN_SURUM      1
#define KERVAN_MAX_GIRDI  8
#define KERVAN_AD_LEN     24

typedef struct __attribute__((packed)) {
    uint64_t sihir;
    uint32_t surum;
    uint32_t girdi_sayisi;
    char     paket_adi[KERVAN_AD_LEN];
    char     paket_surum[16];
    uint32_t toplam_boyut;
    uint32_t saglama;        /* tum veri bloklarinin toplami */
} kervan_baslik_t;

typedef struct __attribute__((packed)) {
    char     ad[KERVAN_AD_LEN];
    uint32_t offset;         /* paket icindeki konum */
    uint32_t boyut;
    uint32_t tur;            /* 0=veri, 1=calistirilabilir */
    uint32_t reserved;
} kervan_girdi_t;

typedef struct {
    int      kurulu;
    char     ad[KERVAN_AD_LEN];
    char     surum[16];
    uint32_t girdi_sayisi;
    uint32_t boyut;
} kervan_paket_t;

void kervan_init(void);

/* Bellekteki bir paketi DOGRULAR (sihir, surum, saglama).
 * 0 = gecerli, -1 = gecersiz. */
int kervan_dogrula(const uint8_t *veri, uint32_t boyut);

/* Paketi kurar: girdilerini BITIG uzerine ayri dosyalar olarak yazar. */
int kervan_kur(const uint8_t *veri, uint32_t boyut);

/* Kurulu paketleri listeler. */
int kervan_liste(kervan_paket_t *out, int max);
int kervan_kurulu_sayisi(void);

#endif
