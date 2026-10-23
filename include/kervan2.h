#ifndef OTUKENOS_KERVAN2_H
#define OTUKENOS_KERVAN2_H
#include <stdint.h>

/* =====================================================================
 * 1.4.9 — KERVAN v2: İMZALI paketler (bkz. araclar/kervan_paketle.py)
 *
 * v1 (kervan.h) yalnız bozulmaya karşı zayıf bir sağlama taşıyordu: herkes
 * geçerli bir v1 paketi üretebilirdi. v2'de her dosyanın SHA-256'sı ve
 * paketin ECDSA P-256 imzası ÇEKİRDEKTE doğrulanır (BearSSL'in çekirdek
 * kopyası). Anahtar 0 = üretim; anahtar 1 = sınama, yalnız TANI yapısında.
 *
 * Kurulum kök dizine yazar; kayıt /KERVAN.LST. Kurallar:
 *   - dosya adları 8.3, [A-Z0-9_-]; dizin yok
 *   - dizge dosyaları (UMAY.ELF, TULPAR.ELF, ...) ve başka paketin
 *     dosyası ÜZERİNE YAZILAMAZ; hiçbir pakete ait olmayan var olan dosya
 *     da (kullanıcının dosyası) ezilmez
 *   - aynı paketin yeni sürümü eskisinin yerini alır
 * ===================================================================== */

#define KERVAN2_AD      24
#define KERVAN2_SURUM   16
#define KERVAN2_ACIKLAMA 96
#define KERVAN2_AZAMI_DOSYA 16
#define KERVAN2_AZAMI_BOY (2u * 1024 * 1024)

/* Ring3'e verilen liste öğesi (SYS_KERVAN_LISTE) */
typedef struct {
    char     ad[KERVAN2_AD];
    char     surum[KERVAN2_SURUM];          /* diskteki paketin sürümü */
    char     kurulu_surum[KERVAN2_SURUM];   /* "" = kurulu değil */
    char     aciklama[KERVAN2_ACIKLAMA];
    char     dosya[13];                     /* .KRV dosyası ("" = yok) */
    uint8_t  durum;                         /* KERVAN2_* */
    uint16_t girdi;
    uint32_t boyut;
    char     neden[64];                     /* geçersizse neden */
} kervan2_oge_t;

enum { KERVAN2_GECERLI = 0, KERVAN2_GECERSIZ = 1, KERVAN2_YALNIZ_KURULU = 2 };

void kervan2_init(void);                    /* /KERVAN.LST okunur */
/* Paket belleğini doğrular. 0 / -1; neden (64 B) doldurulur. */
int  kervan2_dogrula(const uint8_t *veri, uint32_t boy, char *neden);
/* Kök dizindeki .KRV dosyasını kurar. 0 / -1 (neden) */
int  kervan2_kur(const char *krv, char *neden);
/* Kurulu paketi kaldırır (dosyaları silinir). 0 / -1 (neden) */
int  kervan2_kaldir(const char *paket, char *neden);
/* Diskteki paketler + kurulu paketler. Adet. */
int  kervan2_liste(kervan2_oge_t *out, int azami);
int  kervan2_kurulu_mu(const char *paket, char *surum_cikti);
int  kervan2_kurulu_sayisi(void);
/* 1.4.9: ERGENEKON dizge imajı (aynı biçim, aynı anahtarlar, en çok 64 dosya) */
#define KERVAN2_IMAJ_AZAMI_DOSYA 64
int  kervan2_imaj_dogrula(const uint8_t *v, uint32_t boy, char *neden);
int  kervan2_baslik(const uint8_t *v, char ad[KERVAN2_AD], char surum[KERVAN2_SURUM],
                    uint32_t *girdi, uint32_t *anahtar_no);
int  kervan2_girdi(const uint8_t *v, uint32_t i, char ad[13], const uint8_t **veri,
                   uint32_t *boy, const uint8_t **sha256);
void kervan2_kaydi_unut(void);
/* 1.4.9: ağdan indirilen (bellekteki) paketi kur; kurallar dosyadakiyle aynı */
int  kervan2_kur_bellek(const uint8_t *v, uint32_t boy, char *neden);
/* kurulu paketlerin uygulamaları (.ELF): Uygulamalar menüsü */
#define KERVAN2_UYG_AZAMI 8
/* kilit sırası: KERVAN kilidi -> depo_kilit. depo_kilit altında KERVAN'a
 * girecek olan (ERGENEKON) önce bunu alır. Özyinelemeli. */
void kervan2_kilit_al(void);
void kervan2_kilit_birak(void);
int  kervan2_uygulama_sayisi(void);
int  kervan2_uygulama_al(int i, char dosya[13], char paket[KERVAN2_AD]);

extern const uint8_t KERVAN_ANAHTAR_0[65];
extern const uint8_t KERVAN_ANAHTAR_1[65];
#endif
