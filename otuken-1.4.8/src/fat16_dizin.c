/* =====================================================================
 * FAT16 ALT DİZİN DESTEĞİ
 *
 * Bu dosya, FAT16 sürücüsüne alt dizin yeteneğini ekler. Daha önce
 * sürücü YALNIZ kökü tanıyordu: `/a/b.txt` gibi bir yol açılamıyor,
 * `vfs_mkdir` de dürüstçe "desteksiz" dönüyordu. Yarım bir yetenek
 * eklemektense (dizin açılır ama içine girilemez) tam yol çözümlemesi
 * ile birlikte yazıldı.
 *
 * KAPSAM
 *   - Yol çözümleme: "/kut/umay/ayar.txt" bileşen bileşen yürünür.
 *   - Dizin listeleme: kök (sabit alan) ve alt dizin (küme zinciri).
 *   - Dizin oluşturma: küme ayrılır, sıfırlanır, "." ve ".." yazılır,
 *     üst dizine ATTR_DIRECTORY girdisi eklenir.
 *   - 8.3 adlar, büyük/küçük harf duyarsız. Uzun ad (LFN) YOK.
 *
 * SINIR: dizin silme yok; bir dizin tek kümeye sığdığı kadar girdi
 * alır (512 baytlık kümede 16 girdi). Taşma sessizce yutulmaz, hata
 * döner.
 * ===================================================================== */
#include "fat16.h"
#include "depo_kilit.h"
#include "fat16_ic.h"
#include "disk_cache.h"
#include "serial.h"

#define GIRDI_BOY   32
#define ATTR_DIR    0x10

static char kucult(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int ad_esit(const char *a, const char *b)
{
    while (*a && *b) {
        if (kucult(*a) != kucult(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* Bir dizindeki girdileri dolaşır. kume == 0 ise KÖK dizin (sabit alan),
 * değilse küme zinciri. Her girdi için geri çağrı çağrılır; geri çağrı
 * sıfırdan farklı dönerse dolaşma durur. */
typedef int (*girdi_gez_t)(const uint8_t *girdi, uint32_t lba, int indeks,
                           void *baglam);

static int dizin_gez(uint16_t kume, girdi_gez_t geri, void *baglam)
{
    static uint8_t sektor[512];

    if (kume == 0) {
        for (uint32_t s = 0; s < fat16_root_dir_sectors(); s++) {
            uint32_t lba = fat16_root_lba() + s;
            if (disk_cache_read_sectors(0, lba, 1, sektor) != 0) return -1;
            for (int e = 0; e < 16; e++) {
                int r = geri(&sektor[e * GIRDI_BOY], lba, e, baglam);
                if (r) return r;
            }
        }
        return 0;
    }

    uint16_t k = kume;
    while (k >= 2 && k < 0xFFF8) {
        uint32_t taban = fat16_kume_lba(k);
        for (uint32_t s = 0; s < fat16_sectors_per_cluster(); s++) {
            uint32_t lba = taban + s;
            if (disk_cache_read_sectors(0, lba, 1, sektor) != 0) return -1;
            for (int e = 0; e < 16; e++) {
                int r = geri(&sektor[e * GIRDI_BOY], lba, e, baglam);
                if (r) return r;
            }
        }
        k = fat16_sonraki_kume(k);
    }
    return 0;
}

/* --- Bir dizinde ada göre girdi arama --- */
typedef struct {
    const char     *aranan;
    fat16_dirent_t *cikti;
    uint32_t        bulundu_lba;
    int             bulundu_indeks;
    int             bulundu;
} ara_baglam_t;

static int ara_geri(const uint8_t *g, uint32_t lba, int indeks, void *b)
{
    ara_baglam_t *a = (ara_baglam_t *)b;
    if (g[0] == 0x00) return 1;            /* dizin sonu */
    if (g[0] == 0xE5) return 0;
    uint8_t attr = g[11];
    if (attr == 0x0F || (attr & 0x08)) return 0;

    char ad[13];
    fat16_ad_bicimle(g, ad);
    if (!ad_esit(ad, a->aranan)) return 0;

    if (a->cikti) {
        for (int i = 0; i < 13; i++) a->cikti->name[i] = ad[i];
        a->cikti->first_cluster = fat16_oku16(&g[26]);
        a->cikti->size          = fat16_oku32(&g[28]);
        a->cikti->is_dir        = (attr & ATTR_DIR) ? 1 : 0;
    }
    a->bulundu_lba = lba;
    a->bulundu_indeks = indeks;
    a->bulundu = 1;
    return 1;
}

static int dizinde_ara(uint16_t kume, const char *ad, fat16_dirent_t *cikti,
                       uint32_t *lba, int *indeks)
{
    ara_baglam_t a;
    a.aranan = ad; a.cikti = cikti; a.bulundu = 0;
    a.bulundu_lba = 0; a.bulundu_indeks = 0;
    dizin_gez(kume, ara_geri, &a);
    if (!a.bulundu) return -1;
    if (lba)    *lba = a.bulundu_lba;
    if (indeks) *indeks = a.bulundu_indeks;
    return 0;
}

/* Yolun SON bileşenini ayırır, üst dizinin kümesini döndürür.
 * "/a/b/c.txt" -> ust = a/b'nin kumesi, son = "c.txt"
 * "/c.txt"     -> ust = 0 (kok),        son = "c.txt" */
static int yol_coz(const char *yol, uint16_t *ust_kume, char *son, int son_boy)
{
    if (!yol || yol[0] != '/') return -1;
    uint16_t kume = 0;                      /* kök */
    const char *p = yol + 1;

    for (;;) {
        const char *egik = p;
        while (*egik && *egik != '/') egik++;
        int uzunluk = (int)(egik - p);
        if (uzunluk == 0) return -1;
        if (uzunluk > son_boy - 1) return -1;

        if (*egik == 0) {                   /* son bileşen */
            for (int i = 0; i < uzunluk; i++) son[i] = p[i];
            son[uzunluk] = 0;
            *ust_kume = kume;
            return 0;
        }

        char parca[13];
        for (int i = 0; i < uzunluk; i++) parca[i] = p[i];
        parca[uzunluk] = 0;

        fat16_dirent_t d;
        if (dizinde_ara(kume, parca, &d, 0, 0) != 0) return -1;
        if (!d.is_dir) return -1;           /* dosyanın içine girilemez */
        kume = d.first_cluster;
        p = egik + 1;
    }
}

static int fat16_yol_bul__ic(const char *yol, fat16_dirent_t *cikti) {
    uint16_t ust;
    char ad[13];
    if (yol_coz(yol, &ust, ad, sizeof ad) != 0) return -1;
    return dizinde_ara(ust, ad, cikti, 0, 0);
}

/* --- Dizin listeleme (yol ile) --- */
typedef struct {
    fat16_dirent_t *out;
    int max, sayi;
} liste_baglam_t;

static int liste_geri(const uint8_t *g, uint32_t lba, int indeks, void *b)
{
    (void)lba; (void)indeks;
    liste_baglam_t *l = (liste_baglam_t *)b;
    if (g[0] == 0x00) return 1;
    if (g[0] == 0xE5) return 0;
    uint8_t attr = g[11];
    if (attr == 0x0F || (attr & 0x08)) return 0;
    if (l->sayi >= l->max) return 1;

    fat16_ad_bicimle(g, l->out[l->sayi].name);
    l->out[l->sayi].first_cluster = fat16_oku16(&g[26]);
    l->out[l->sayi].size          = fat16_oku32(&g[28]);
    l->out[l->sayi].is_dir        = (attr & ATTR_DIR) ? 1 : 0;
    l->sayi++;
    return 0;
}

static int fat16_listele_yol__ic(const char *yol, fat16_dirent_t *out, int max) {
    if (!yol || !out || max <= 0) return 0;
    uint16_t kume = 0;
    if (!(yol[0] == '/' && yol[1] == 0)) {
        fat16_dirent_t d;
        if (fat16_yol_bul(yol, &d) != 0) return -1;
        if (!d.is_dir) return -1;
        kume = d.first_cluster;
    }
    liste_baglam_t l = { out, max, 0 };
    if (dizin_gez(kume, liste_geri, &l) < 0) return -1;
    return l.sayi;
}

/* --- Dizinde boş girdi yeri bulma --- */
typedef struct { uint32_t lba; int indeks; int bulundu; } bos_baglam_t;

static int bos_geri(const uint8_t *g, uint32_t lba, int indeks, void *b)
{
    bos_baglam_t *s = (bos_baglam_t *)b;
    if (g[0] == 0x00 || g[0] == 0xE5) {
        s->lba = lba; s->indeks = indeks; s->bulundu = 1;
        return 1;
    }
    return 0;
}

static int fat16_dizin_yarat__ic(const char *yol) {
    if (!fat16_bagli_mi()) return -1;

    uint16_t ust;
    char ad[13];
    if (yol_coz(yol, &ust, ad, sizeof ad) != 0) return -1;

    /* Zaten var mı: sessizce başarılı DÖNMEYİZ, açıkça hata veririz. */
    fat16_dirent_t varolan;
    if (dizinde_ara(ust, ad, &varolan, 0, 0) == 0) return -2;

    uint16_t kume = fat16_bos_kume_bul();
    if (kume == 0) return -3;
    if (fat16_kume_yaz(kume, 0xFFFF) != 0) return -4;

    /* Yeni dizin kümesini sıfırla, "." ve ".." gir. */
    static uint8_t tampon[512];
    for (int i = 0; i < 512; i++) tampon[i] = 0;

    uint8_t nokta[11], nokta2[11];
    for (int i = 0; i < 11; i++) { nokta[i] = ' '; nokta2[i] = ' '; }
    nokta[0] = '.';
    nokta2[0] = '.'; nokta2[1] = '.';

    for (int i = 0; i < 11; i++) tampon[i] = nokta[i];
    tampon[11] = ATTR_DIR;
    fat16_yaz16(&tampon[26], kume);

    for (int i = 0; i < 11; i++) tampon[32 + i] = nokta2[i];
    tampon[32 + 11] = ATTR_DIR;
    fat16_yaz16(&tampon[32 + 26], ust);      /* kök için 0 doğru değerdir */

    uint32_t taban = fat16_kume_lba(kume);
    if (disk_cache_write_sectors(0, taban, 1, tampon) != 0) {
        fat16_kume_yaz(kume, 0x0000);        /* geri al */
        return -5;
    }
    for (uint32_t s = 1; s < fat16_sectors_per_cluster(); s++) {
        static uint8_t sifir[512];
        for (int i = 0; i < 512; i++) sifir[i] = 0;
        if (disk_cache_write_sectors(0, taban + s, 1, sifir) != 0) {
            fat16_kume_yaz(kume, 0x0000);
            return -5;
        }
    }

    /* Üst dizine girdiyi ekle. */
    bos_baglam_t bos = { 0, 0, 0 };
    dizin_gez(ust, bos_geri, &bos);
    if (!bos.bulundu) {
        fat16_kume_yaz(kume, 0x0000);
        return -6;                            /* dizin dolu: sessiz kabul YOK */
    }

    static uint8_t sektor[512];
    if (disk_cache_read_sectors(0, bos.lba, 1, sektor) != 0) {
        fat16_kume_yaz(kume, 0x0000);
        return -7;
    }
    uint8_t *g = &sektor[bos.indeks * GIRDI_BOY];
    for (int i = 0; i < GIRDI_BOY; i++) g[i] = 0;
    fat16_ad_83_yap(ad, g);
    g[11] = ATTR_DIR;
    fat16_yaz16(&g[26], kume);
    fat16_yaz32(&g[28], 0);                   /* dizin boyutu her zaman 0 */

    if (disk_cache_write_sectors(0, bos.lba, 1, sektor) != 0) {
        fat16_kume_yaz(kume, 0x0000);
        return -8;
    }
    return 0;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int fat16_yol_bul(const char *yol, fat16_dirent_t *cikti) {
    ozkilit_al(&depo_kilit);
    int r = fat16_yol_bul__ic(yol, cikti);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat16_listele_yol(const char *yol, fat16_dirent_t *out, int max) {
    ozkilit_al(&depo_kilit);
    int r = fat16_listele_yol__ic(yol, out, max);
    ozkilit_birak(&depo_kilit);
    return r;
}

int fat16_dizin_yarat(const char *yol) {
    ozkilit_al(&depo_kilit);
    int r = fat16_dizin_yarat__ic(yol);
    ozkilit_birak(&depo_kilit);
    return r;
}
