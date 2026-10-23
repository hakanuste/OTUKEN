/* =====================================================================
 * 1.4.9 — ASENA: FAT16 denetimi ve onarımı (bkz. include/asena.h)
 *
 * Bütün iş depo_kilit altında, bellekteki FAT kopyası üzerinde yapılır.
 * Denetim ve onarım AYNI yoldan geçer; yalnız onarımda sonuç diske yazılır.
 * Böylece "denetle" ne bulduysa "onar" tam olarak onu düzeltir.
 * ===================================================================== */
#include "asena.h"
#include "fat16.h"
#include "fat16_ic.h"
#include "disk_cache.h"
#include "depo_kilit.h"
#include "heap.h"
#include "serial.h"
#include "log.h"

#define EOC        0xFFFFu
#define AZAMI_GIRIS 2048
#define AZAMI_BULGU 64

typedef struct {
    uint32_t lba;          /* girişin bulunduğu sektör */
    uint16_t ofs;          /* sektör içindeki konum */
    uint16_t ust;          /* üst dizinin giriş kimliği (0 kök) */
    uint16_t ilk;
    uint8_t  dizin;
    uint8_t  degisti;
    uint8_t  silindi;
    uint32_t boy;
    char     ad[13];
} giris_t;

static giris_t      *gir;        /* 1..n (0 kullanılmaz: sahip=0 "yok") */
static uint32_t      gir_n;
static asena_bulgu_t bulgular[AZAMI_BULGU];
static uint32_t      bulgu_n;

static const char *const TUR_ADI[ASENA_TUR_SAYISI] = {
    "FAT kopyalari farkli", "kayip kume", "capraz bagli kume", "kirik zincir",
    "dongulu zincir", "boy zincirden buyuk", "zincir boydan uzun", "gecersiz baslangic kumesi"
};
const char *asena_tur_adi(uint32_t t) { return t < ASENA_TUR_SAYISI ? TUR_ADI[t] : "?"; }

static void yol_yaz(char *d, uint32_t k) {
    /* "UST/AD" (tek düzey üst yeter; derin yol kısaltılır) */
    int n = 0;
    if (k && gir[k].ust) {
        const char *u = gir[gir[k].ust].ad;
        while (*u && n < 20) d[n++] = *u++;
        d[n++] = '/';
    }
    const char *a = k ? gir[k].ad : "";
    while (*a && n < 39) d[n++] = *a++;
    d[n] = 0;
}

static void bulgu_ekle(asena_rapor_t *r, uint32_t tur, uint32_t k, uint32_t deger) {
    r->sayi[tur]++;
    r->bulgu++;
    if (bulgu_n < AZAMI_BULGU) {
        asena_bulgu_t *b = &bulgular[bulgu_n++];
        b->tur = tur; b->deger = deger;
        yol_yaz(b->yol, k);
    }
}

/* dizin sektörlerini gezip girişleri toplar */
static int dizin_topla(uint32_t lba, uint32_t sektor, uint16_t ust, uint8_t *sb) {
    for (uint32_t s = 0; s < sektor; s++) {
        if (disk_cache_read_sectors(0, lba + s, 1, sb) != 0) return -1;
        for (int e = 0; e < 16; e++) {
            uint8_t *d = sb + e * 32;
            if (d[0] == 0x00) return 1;                  /* dizin sonu */
            if (d[0] == 0xE5 || d[11] == 0x0F || (d[11] & 0x08)) continue;
            if (d[0] == '.') continue;                    /* . ve .. */
            if (gir_n + 1 >= AZAMI_GIRIS) return -2;     /* tablo doldu: onarım yapılmaz */
            giris_t *g = &gir[++gir_n];
            g->lba = lba + s; g->ofs = (uint16_t)(e * 32); g->ust = ust;
            g->ilk = fat16_oku16(d + 26); g->boy = fat16_oku32(d + 28);
            g->dizin = (d[11] & 0x10) ? 1 : 0;
            g->degisti = 0; g->silindi = 0;
            fat16_ad_bicimle(d, g->ad);
        }
    }
    return 0;
}

int asena_tara(int onar, asena_rapor_t *r) {
    for (unsigned i = 0; i < sizeof *r; i++) ((uint8_t *)r)[i] = 0;
    bulgu_n = 0;
    ozkilit_al(&depo_kilit);
    fat16_geo_t g;
    uint16_t *fat = 0, *sahip = 0;
    uint8_t *sb = 0;
    int sonuc = -1;
    if (fat16_geometri(&g) != 0) { r->hata = 1; goto son; }
    uint32_t N = g.kume_sayisi, ust_sinir = N + 2;
    uint32_t fat_giris = g.fat_sektor * 256;
    if (ust_sinir > fat_giris) ust_sinir = fat_giris;
    uint32_t kume_bayt = g.sektor_kume * 512;
    fat = (uint16_t *)kmalloc(g.fat_sektor * 512);
    sahip = (uint16_t *)kmalloc(ust_sinir * 2);
    sb = (uint8_t *)kmalloc(512);
    if (!gir) gir = (giris_t *)kmalloc(sizeof(giris_t) * AZAMI_GIRIS);
    if (!fat || !sahip || !sb || !gir) { r->hata = 2; goto son; }
    for (uint32_t i = 0; i < ust_sinir; i++) sahip[i] = 0;

    /* 1) FAT kopyaları */
    for (uint32_t s = 0; s < g.fat_sektor; s++)
        if (disk_cache_read_sectors(0, g.fat_lba + s, 1, (uint8_t *)fat + s * 512) != 0) { r->hata = 3; goto son; }
    for (uint32_t f = 1; f < g.fat_sayisi; f++) {
        uint32_t fark = 0;
        for (uint32_t s = 0; s < g.fat_sektor; s++) {
            if (disk_cache_read_sectors(0, g.fat_lba + f * g.fat_sektor + s, 1, sb) != 0) { r->hata = 3; goto son; }
            const uint8_t *a = (const uint8_t *)fat + s * 512;
            for (int i = 0; i < 512; i++) if (a[i] != sb[i]) { fark++; break; }
        }
        if (fark) bulgu_ekle(r, ASENA_FAT_FARK, 0, fark);
    }

    /* 2) girişler: kök, sonra her alt dizin (genişlik önce) */
    gir_n = 0;
    { int t0 = dizin_topla(g.kok_lba, g.kok_sektor, 0, sb);
      if (t0 < 0) { r->hata = t0 == -2 ? 4 : 3; goto son; } }
    for (uint32_t k = 1; k <= gir_n; k++) {
        giris_t *e = &gir[k];
        uint32_t c = e->ilk, onceki = 0, uzunluk = 0;
        if (e->dizin) r->dizin++; else r->dosya++;
        if (c == 0) {
            if (!e->dizin && e->boy > 0) {       /* boy var, zincir yok */
                bulgu_ekle(r, ASENA_BOY_FAZLA, k, e->boy);
                e->boy = 0; e->degisti = 1;
            }
            if (e->dizin) { bulgu_ekle(r, ASENA_GECERSIZ_BAS, k, 0); e->silindi = 1; e->degisti = 1; }
            continue;
        }
        if (c < 2 || c >= ust_sinir) {
            bulgu_ekle(r, ASENA_GECERSIZ_BAS, k, c);
            if (e->dizin) e->silindi = 1;
            e->ilk = 0; e->boy = 0; e->degisti = 1;
            continue;
        }
        for (;;) {
            if (sahip[c] == k) {                  /* döngü: önceki kümede bitir */
                bulgu_ekle(r, ASENA_DONGU, k, c);
                fat[onceki] = EOC;
                break;
            }
            if (sahip[c] != 0) {                  /* çapraz bağ: bu dosya burada biter */
                bulgu_ekle(r, ASENA_CAPRAZ, k, c);
                if (onceki == 0) { e->ilk = 0; e->degisti = 1; if (e->dizin) e->silindi = 1; }
                else fat[onceki] = EOC;
                break;
            }
            if (fat[c] == 0xFFF7) {               /* kötü küme zincirde: ÖNCEKİNDE bitir, işaret kalır */
                bulgu_ekle(r, ASENA_KIRIK, k, c);
                if (onceki == 0) { e->ilk = 0; e->degisti = 1; if (e->dizin) e->silindi = 1; }
                else fat[onceki] = EOC;
                break;
            }
            sahip[c] = (uint16_t)k;
            uzunluk++;
            uint16_t sonraki = fat[c];
            if (sonraki >= 0xFFF8) break;
            if (sonraki >= 2 && sonraki < ust_sinir) { onceki = c; c = sonraki; continue; }
            bulgu_ekle(r, ASENA_KIRIK, k, c);     /* boş, kötü, ayrılmış ya da aralık dışı */
            fat[c] = EOC;
            break;
        }
        if (e->silindi) continue;
        if (e->dizin) {
            /* alt dizinin (kesilmiş) zinciri boyunca girişleri topla */
            uint32_t d = e->ilk, adim = 0;
            while (d >= 2 && d < ust_sinir && adim++ < uzunluk) {
                int t = dizin_topla(g.veri_lba + (d - 2) * g.sektor_kume, g.sektor_kume, (uint16_t)k, sb);
                if (t < 0) { r->hata = t == -2 ? 4 : 3; goto son; }
                if (t > 0) break;
                if (fat[d] >= 0xFFF8) break;
                d = fat[d];
            }
            continue;
        }
        if (e->ilk == 0) uzunluk = 0;
        uint64_t kapasite = (uint64_t)uzunluk * kume_bayt;
        uint32_t gereken = (uint32_t)((e->boy + kume_bayt - 1) / kume_bayt);
        if ((uint64_t)e->boy > kapasite) {
            bulgu_ekle(r, ASENA_BOY_FAZLA, k, e->boy);
            e->boy = (uint32_t)kapasite; e->degisti = 1;
        } else if (uzunluk > gereken) {
            bulgu_ekle(r, ASENA_ZINCIR_FAZLA, k, uzunluk - gereken);
            /* gereken'inci kümede bitir, gerisini bırak (sahipsiz -> boşaltılır) */
            uint32_t x = e->ilk, i = 1;
            if (gereken == 0) { e->ilk = 0; e->degisti = 1; }
            while (x >= 2 && x < ust_sinir) {
                uint16_t nx = fat[x];
                if (i > gereken) { sahip[x] = 0; fat[x] = 0; }   /* fazlası boşaltılır */
                if (i == gereken) fat[x] = EOC;
                if (nx >= 0xFFF8 || i >= uzunluk) break;
                x = nx; i++;
            }
        }
    }

    /* 3) kayıp kümeler: ayrılmış ama hiçbir girişe ait değil */
    uint32_t kayip = 0;
    for (uint32_t c = 2; c < ust_sinir; c++) {
        if (fat[c] != 0 && fat[c] != 0xFFF7 && sahip[c] == 0) {
            kayip++;
            fat[c] = 0;
        }
    }
    if (kayip) bulgu_ekle(r, ASENA_KAYIP, 0, kayip);

    /* 4) onarım: FAT (bütün kopyalara) ve değişen girişler */
    if (onar && r->bulgu) {
        for (uint32_t f = 0; f < g.fat_sayisi; f++)
            for (uint32_t s = 0; s < g.fat_sektor; s++)
                if (disk_cache_write_sectors(0, g.fat_lba + f * g.fat_sektor + s, 1, (uint8_t *)fat + s * 512) != 0) { r->hata = 3; goto son; }
        for (uint32_t k = 1; k <= gir_n; k++) {
            giris_t *e = &gir[k];
            if (!e->degisti) continue;
            if (disk_cache_read_sectors(0, e->lba, 1, sb) != 0) { r->hata = 3; goto son; }
            uint8_t *d = sb + e->ofs;
            if (e->silindi) d[0] = 0xE5;
            else { fat16_yaz16(d + 26, e->ilk); fat16_yaz32(d + 28, e->boy); }
            if (disk_cache_write_sectors(0, e->lba, 1, sb) != 0) { r->hata = 3; goto son; }
            r->onarilan++;
        }
        r->onarilan += r->sayi[ASENA_FAT_FARK] + (kayip ? 1 : 0);
        fat16_ipucu_sifirla();
        serial_write("ASENA: onarim yazildi, bulgu=");
        serial_write_hex(r->bulgu);
        serial_write("\n");
        log_write(LOG_UYARI, "ASENA", "dosya sistemi onarildi");
    }
    r->kume_sayisi = N;
    for (uint32_t c = 2; c < ust_sinir; c++) if (fat[c] == 0) r->bos_kume++;
    sonuc = 0;
son:
    if (fat) kfree(fat);
    if (sahip) kfree(sahip);
    if (sb) kfree(sb);
    ozkilit_birak(&depo_kilit);
    return sonuc;
}

int asena_bulgu(int i, asena_bulgu_t *b) {
    if (i < 0 || (uint32_t)i >= bulgu_n) return -1;
    *b = bulgular[i];
    return 0;
}
