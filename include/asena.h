#ifndef OTUKENOS_ASENA_H
#define OTUKENOS_ASENA_H
/* =====================================================================
 * 1.4.9 — ASENA: kurtarma. FAT16 birimini denetler ve onarır.
 *
 * Denetlenen: FAT kopyalarının eşitliği, her dosya/dizin zincirinin
 * geçerliliği (geçersiz başlangıç, boş/kötü/aralık dışı küme gösteren
 * zincir, döngü), çapraz bağlı kümeler (iki dosyanın paylaştığı), boy ile
 * zincir uzunluğunun uyumu, sahipsiz (kayıp) kümeler. Alt dizinler dahil.
 * Onarım fsck.fat'ın yaptığıyla aynı yöndedir: zincir kırıksa orada biter,
 * çapraz bağda SONRAKİ dosya paylaşılan kümeden önce kesilir, boy zincire
 * uydurulur, fazla zincir ve kayıp kümeler boşaltılır, FAT kopyaları
 * birinciden yazılır. Veri KURTARILMAZ: kesilen kısım gider.
 * Dizge dosyalarının denetimi ve imajdan geri yüklenmesi ERGENEKON imajıyla
 * yapılır (bkz. ergenekon.h).
 * ===================================================================== */
#include <stdint.h>

enum {
    ASENA_FAT_FARK = 0, ASENA_KAYIP, ASENA_CAPRAZ, ASENA_KIRIK, ASENA_DONGU,
    ASENA_BOY_FAZLA, ASENA_ZINCIR_FAZLA, ASENA_GECERSIZ_BAS, ASENA_TUR_SAYISI
};

/* Ring3 ile aynı düzen (64 bayt) */
typedef struct {
    uint32_t sayi[ASENA_TUR_SAYISI];  /* bulgu türüne göre adet */
    uint32_t dosya, dizin;            /* taranan */
    uint32_t onarilan;                /* onarımda yazılan düzeltme */
    uint32_t bulgu;                   /* toplam bulgu */
    uint32_t bos_kume, kume_sayisi;
    uint32_t hata;                    /* 0 tamam; 1 birim bağlı değil; 2 bellek; 3 disk */
    uint32_t b;
} asena_rapor_t;
_Static_assert(sizeof(asena_rapor_t) == 64, "asena rapor");

typedef struct {
    uint32_t tur;                     /* ASENA_* */
    uint32_t deger;                   /* küme ya da boy */
    char     yol[40];                 /* "DIZIN/AD.UZ" */
} asena_bulgu_t;
_Static_assert(sizeof(asena_bulgu_t) == 48, "asena bulgu");

/* onar=0: yalnız denetle (diske yazmaz). 0 / -1 */
int asena_tara(int onar, asena_rapor_t *r);
/* son taramanın i. bulgusu; 0 / -1 */
int asena_bulgu(int i, asena_bulgu_t *b);
const char *asena_tur_adi(uint32_t tur);
#endif
