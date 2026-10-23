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
 * 1.4.9: alt dizine YAZMA (fat16_yol_yaz), SONUNA EKLEME (fat16_yol_ekle:
 * yalnız yeni veri yazılır, dosya baştan yazılmaz) ve SİLME (fat16_yol_sil;
 * dizin yalnız boşsa). Alt dizin dolunca zincirine yeni küme eklenir
 * (kök dizin sabit boydadır, dolarsa hata). Yazma sırası: önce yeni
 * kümeler ve veri, sonra dizin girdisi, EN SON eski zincir bırakılır;
 * yarıda kesilirse sonuç sahipsiz küme olur, iki dosyanın paylaştığı küme
 * olmaz (ASENA onarır).
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
    uint32_t say = 0;
    while (k >= 2 && k < 0xFFF8) {
        if (say++ >= 65536) return -1;     /* bozuk zincir döngüsü: kilidi bırak */
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

static int dizin_yuva(uint16_t ust, const char *ad, uint32_t *lba, int *indeks,
                      int *var_mi, fat16_dirent_t *eski);
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

    /* Üst dizine girdiyi ekle (1.4.9: alt dizin doluysa zinciri uzar). */
    bos_baglam_t bos = { 0, 0, 0 };
    {
        int var; fat16_dirent_t e;
        if (dizin_yuva(ust, ad, &bos.lba, &bos.indeks, &var, &e) != 0 || var) {
            fat16_kume_yaz(kume, 0x0000);
            return -6;                        /* dizin dolu: sessiz kabul YOK */
        }
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

/* ===================== 1.4.9: alt dizine yazma ===================== */
#define AZAMI_KUME 4096
static uint32_t kume_bayt(void) { return fat16_sectors_per_cluster() * 512u; }

static void zincir_bosalt(uint16_t k) {
    uint32_t say = 0;
    while (k >= 2 && k < 0xFFF8 && say++ < 65536) {
        uint16_t n = fat16_sonraki_kume(k);
        fat16_kume_yaz(k, 0x0000);
        k = n;
    }
}

/* n küme ayırıp zincirler (onceki != 0 ise ona eklenir). İlk küme; 0 hata
 * (bu çağrıda ayrılanlar geri alınır, onceki yine zincir sonu olur). */
static uint16_t zincir_ayir(uint32_t n, uint16_t onceki) {
    static uint16_t liste[AZAMI_KUME];
    if (n == 0 || n > AZAMI_KUME) return 0;
    uint16_t ilk = 0, son = onceki;
    uint32_t a = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t k = fat16_bos_kume_bul();
        if (k == 0 || fat16_kume_yaz(k, 0xFFFF) != 0) goto geri;
        liste[a++] = k;
        if (son && fat16_kume_yaz(son, k) != 0) goto geri;
        if (!ilk) ilk = k;
        son = k;
    }
    return ilk;
geri:
    for (uint32_t i = 0; i < a; i++) fat16_kume_yaz(liste[i], 0x0000);
    if (onceki) fat16_kume_yaz(onceki, 0xFFFF);
    return 0;
}

/* k kümesinin 'ofs' baytından başlayarak zincir boyunca n bayt yaz */
static int veri_yaz(uint16_t k, uint32_t ofs, const uint8_t *d, uint32_t n) {
    static uint8_t s[512];
    uint32_t kb = kume_bayt();
    while (n > 0) {
        if (k < 2 || k >= 0xFFF8) return -1;
        while (ofs < kb && n > 0) {
            uint32_t lba = fat16_kume_lba(k) + ofs / 512, so = ofs % 512;
            uint32_t parca = 512 - so; if (parca > n) parca = n;
            if (so != 0 || parca != 512) { if (disk_cache_read_sectors(0, lba, 1, s) != 0) return -1; }
            for (uint32_t i = 0; i < parca; i++) s[so + i] = d[i];
            if (disk_cache_write_sectors(0, lba, 1, s) != 0) return -1;
            d += parca; n -= parca; ofs += parca;
        }
        if (n == 0) break;
        k = fat16_sonraki_kume(k); ofs = 0;
    }
    return 0;
}

static int kume_sifirla(uint16_t k) {
    static uint8_t z[512];
    for (int i = 0; i < 512; i++) z[i] = 0;
    for (uint32_t s = 0; s < fat16_sectors_per_cluster(); s++)
        if (disk_cache_write_sectors(0, fat16_kume_lba(k) + s, 1, z) != 0) return -1;
    return 0;
}

/* girdinin yeri: var olan ya da boş yuva; alt dizin doluysa zincir uzar */
static int dizin_yuva(uint16_t ust, const char *ad, uint32_t *lba, int *indeks,
                      int *var_mi, fat16_dirent_t *eski) {
    if (dizinde_ara(ust, ad, eski, lba, indeks) == 0) { *var_mi = 1; return 0; }
    *var_mi = 0;
    bos_baglam_t b = { 0, 0, 0 };
    dizin_gez(ust, bos_geri, &b);
    if (b.bulundu) { *lba = b.lba; *indeks = b.indeks; return 0; }
    if (ust == 0) return -1;                      /* kök sabit boyda */
    uint16_t k = ust, son = 0;
    uint32_t say = 0;
    while (k >= 2 && k < 0xFFF8 && say++ < 65536) { son = k; k = fat16_sonraki_kume(k); }
    uint16_t yeni = zincir_ayir(1, son);
    if (!yeni) return -1;
    if (kume_sifirla(yeni) != 0) { fat16_kume_yaz(yeni, 0); fat16_kume_yaz(son, 0xFFFF); return -1; }
    *lba = fat16_kume_lba(yeni); *indeks = 0;
    return 0;
}

static int girdi_yaz(uint32_t lba, int indeks, const char *ad, uint8_t attr, uint16_t ilk, uint32_t boy) {
    static uint8_t s[512];
    if (disk_cache_read_sectors(0, lba, 1, s) != 0) return -1;
    uint8_t *g = &s[indeks * GIRDI_BOY];
    for (int i = 0; i < GIRDI_BOY; i++) g[i] = 0;
    fat16_ad_83_yap(ad, g);
    g[11] = attr;
    fat16_yaz16(&g[26], ilk);
    fat16_yaz32(&g[28], boy);
    return disk_cache_write_sectors(0, lba, 1, s);
}

static int ad83_gecerli(const char *a) {
    int ana = 0, uz = 0, nokta = 0;
    for (; *a; a++) {
        char c = *a;
        if (c == '.') { if (nokta || !ana) return 0; nokta = 1; continue; }
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
        if (nokta) { if (++uz > 3) return 0; } else if (++ana > 8) return 0;
    }
    return ana > 0 && (!nokta || uz > 0);
}

static int fat16_yol_yaz__ic(const char *yol, const uint8_t *veri, uint32_t boy, int ekle) {
    if (!fat16_bagli_mi() || !fat16_sectors_per_cluster()) return -1;
    uint16_t ust; char ad[13];
    if (yol_coz(yol, &ust, ad, sizeof ad) != 0 || !ad83_gecerli(ad)) return -1;
    if (!ekle && ust == 0) return fat16_write_file(ad, veri, boy);   /* kök: eski, sınanmış yol */
    uint32_t lba; int indeks, var;
    fat16_dirent_t eski;
    if (dizin_yuva(ust, ad, &lba, &indeks, &var, &eski) != 0) return -1;
    if (var && eski.is_dir) return -1;
    uint32_t kb = kume_bayt();
    if (ekle && var && eski.first_cluster >= 2 && eski.size > 0) {
        /* SONUNA EKLE: yalnız yeni bayt yazılır */
        uint16_t k = eski.first_cluster, son = k;
        uint32_t say = 0;
        while (k >= 2 && k < 0xFFF8 && say++ < 65536) { son = k; k = fat16_sonraki_kume(k); }
        uint32_t dolu = eski.size % kb; if (dolu == 0) dolu = kb;
        uint32_t bosluk = kb - dolu;
        uint32_t ilk_parca = boy < bosluk ? boy : bosluk;
        if (ilk_parca && veri_yaz(son, dolu, veri, ilk_parca) != 0) return -1;
        uint32_t kalan = boy - ilk_parca;
        if (kalan) {
            uint32_t n = (kalan + kb - 1) / kb;
            uint16_t yeni = zincir_ayir(n, son);
            if (!yeni) return -1;
            if (veri_yaz(yeni, 0, veri + ilk_parca, kalan) != 0) { zincir_bosalt(yeni); fat16_kume_yaz(son, 0xFFFF); return -1; }
        }
        if (girdi_yaz(lba, indeks, ad, 0x20, eski.first_cluster, eski.size + boy) != 0) return -1;
        return (int)boy;
    }
    /* YENİ ya da BAŞTAN: önce yeni zincir + veri, sonra girdi, en son eski zincir */
    uint16_t yeni = 0;
    if (boy > 0) {
        uint32_t n = (boy + kb - 1) / kb;
        yeni = zincir_ayir(n, 0);
        if (!yeni) return -1;
        if (veri_yaz(yeni, 0, veri, boy) != 0) { zincir_bosalt(yeni); return -1; }
    }
    if (girdi_yaz(lba, indeks, ad, 0x20, yeni, boy) != 0) { if (yeni) zincir_bosalt(yeni); return -1; }
    if (var && eski.first_cluster >= 2) zincir_bosalt(eski.first_cluster);
    serial_write("BITIG(fat16): yazildi '"); serial_write(yol); serial_write("'\n");
    return (int)boy;
}

typedef struct { int dolu; } bos_mu_t;
static int bos_mu_geri(const uint8_t *g, uint32_t lba, int indeks, void *b) {
    (void)lba; (void)indeks;
    if (g[0] == 0x00) return 1;
    if (g[0] == 0xE5 || g[11] == 0x0F || (g[11] & 0x08) || g[0] == '.') return 0;
    ((bos_mu_t *)b)->dolu = 1;
    return 1;
}

static int fat16_yol_sil__ic(const char *yol) {
    if (!fat16_bagli_mi()) return -1;
    uint16_t ust; char ad[13];
    if (yol_coz(yol, &ust, ad, sizeof ad) != 0) return -1;
    if (ust == 0) {
        fat16_dirent_t d;
        if (dizinde_ara(0, ad, &d, 0, 0) != 0) return -1;
        if (!d.is_dir) return fat16_sil(ad);
    }
    fat16_dirent_t d; uint32_t lba; int indeks;
    if (dizinde_ara(ust, ad, &d, &lba, &indeks) != 0) return -1;
    if (d.is_dir) {
        bos_mu_t b = { 0 };
        dizin_gez(d.first_cluster, bos_mu_geri, &b);
        if (b.dolu) return -2;                       /* boş olmayan dizin silinmez */
    }
    static uint8_t s[512];
    if (disk_cache_read_sectors(0, lba, 1, s) != 0) return -1;
    s[indeks * GIRDI_BOY] = 0xE5;
    if (disk_cache_write_sectors(0, lba, 1, s) != 0) return -1;
    if (d.first_cluster >= 2) zincir_bosalt(d.first_cluster);
    return 0;
}

int fat16_yol_yaz(const char *yol, const uint8_t *veri, uint32_t boy) {
    ozkilit_al(&depo_kilit);
    int r = fat16_yol_yaz__ic(yol, veri, boy, 0);
    ozkilit_birak(&depo_kilit);
    return r;
}
int fat16_yol_ekle(const char *yol, const uint8_t *veri, uint32_t boy) {
    ozkilit_al(&depo_kilit);
    int r = fat16_yol_yaz__ic(yol, veri, boy, 1);
    ozkilit_birak(&depo_kilit);
    return r;
}
int fat16_yol_sil(const char *yol) {
    ozkilit_al(&depo_kilit);
    int r = fat16_yol_sil__ic(yol);
    ozkilit_birak(&depo_kilit);
    return r;
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
