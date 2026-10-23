/* =====================================================================
 * 1.4.9 — DIZIN-YAZ-TEST (yalniz TANI): FAT16 alt dizine yazma, ekleme,
 * silme ve TORE yazma koku.
 *     1 yaz + geri oku (alt dizin, cok kumeli), bastan yaz (kisalt)
 *     2 sonuna ekle: kume sinirini asan parcalar; icerik = parcalarin birlesimi
 *     4 dizin dolunca zincir uzar: 40 dosya, hepsi listede ve okunur
 *     8 sil: dolu dizin SILINMEZ; dosyalar ve bos dizin silinir; sahipsiz
 *       kume kalmaz (ASENA: KAYIP yok)
 *    16 ASENA denetimi her adimda temiz (capraz bag, kayip kume yok)
 *    32 TORE yazma koku: kok altina izin, disina ret, ".." ile kacis yok
 *    64 UMAY kendi kokune yazdi: /KUT/UMAY/GUNLUK.TXT var ve dolu
 * (ana makinede: testler.sh diski fsck.fat -n ile ayrica denetler)
 * ===================================================================== */
#include "sinama_dizin.h"
#include "sinama_uyg.h"
#include "fat16.h"
#include "fat16_ic.h"
#include "vfs.h"
#include "asena.h"
#include "tore.h"
#include "thread.h"
#include "timer.h"
#include "serial.h"
#include "heap.h"

static void yaz_sayi(uint64_t v) {
    char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char c[24]; int k = 0; while (n) c[k++] = t[--n]; c[k] = 0; serial_write(c);
}
static int oku(const char *yol, uint8_t *b, int azami) {
    int fd = vfs_open(yol);
    if (fd < 0) return -1;
    int n = 0;
    for (;;) { int r = vfs_read(fd, b + n, (uint64_t)(azami - n)); if (r <= 0) break; n += r; if (n >= azami) break; }
    vfs_close(fd);
    return n;
}
static int temiz(void) { asena_rapor_t r; return asena_tara(0, &r) == 0 && r.bulgu == 0; }
static void ad_yap(char *d, const char *on, int i, const char *son) {
    int n = 0;
    while (*on) d[n++] = *on++;
    d[n++] = (char)('0' + i / 10); d[n++] = (char)('0' + i % 10);
    while (*son) d[n++] = *son++;
    d[n] = 0;
}

static void gorev(void) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    int hata = 0;
    /* bos kume sayisini olcen oteki disk sinamalari (KERVAN, ASENA, UYG)
     * bitmeden baslama: esanli yazimlar birbirinin olcumunu bozar.
     * UYG-TEST zincirin sonudur (KERVAN ve ASENA'yi bekler). */
    for (uint64_t t0 = timer_get_ticks(); !sinama_uyg_bitti() && timer_get_ticks() - t0 < 600 * (uint64_t)hz; ) thread_sleep(1);
    uint8_t *a = (uint8_t *)kmalloc(16384), *b = (uint8_t *)kmalloc(16384);
    if (!a || !b) { serial_write("DIZIN-YAZ-TEST: bellek yok -> HATA\n"); thread_exit(); }
    uint32_t bos0 = fat16_bos_kume_sayisi();
    if (!temiz()) { serial_write("DIZIN-YAZ-TEST:   baslangicta disk temiz degil\n"); hata |= 16; }

    /* 1) yaz / geri oku / bastan yaz */
    if (fat16_dizin_yarat("/DYZ") != 0 || fat16_dizin_yarat("/DYZ/ALT") != 0) hata |= 1;
    for (int i = 0; i < 3000; i++) a[i] = (uint8_t)(i * 7 + 3);
    if (fat16_yol_yaz("/DYZ/A.TXT", a, 3000) != 3000) hata |= 1;
    if (oku("/DYZ/A.TXT", b, 16384) != 3000) hata |= 1;
    for (int i = 0; i < 3000; i++) if (a[i] != b[i]) { hata |= 1; break; }
    if (fat16_yol_yaz("/DYZ/A.TXT", a + 100, 700) != 700 || oku("/DYZ/A.TXT", b, 16384) != 700) hata |= 1;
    for (int i = 0; i < 700; i++) if (a[100 + i] != b[i]) { hata |= 1; break; }
    if (!temiz()) hata |= 16;

    /* 2) sonuna ekle: 20 parca, boylar kume sinirlarini kesecek sekilde */
    uint32_t top = 0;
    for (int p = 0; p < 20; p++) {
        uint32_t n = 37 + (uint32_t)p * 61;
        for (uint32_t i = 0; i < n; i++) a[top + i] = (uint8_t)(p * 31 + i);
        if (fat16_yol_ekle("/DYZ/G.LOG", a + top, n) != (int)n) { hata |= 2; break; }
        top += n;
    }
    int gn = oku("/DYZ/G.LOG", b, 16384);
    if (gn != (int)top) hata |= 2;
    for (uint32_t i = 0; i < top && gn == (int)top; i++) if (a[i] != b[i]) { hata |= 2; break; }
    if (!temiz()) hata |= 16;

    /* 4) 40 dosya: dizin (512 B kume = 16 girdi) iki kez uzamali */
    char yol[32];
    for (int i = 0; i < 40; i++) {
        ad_yap(yol, "/DYZ/ALT/D", i, ".DAT");
        uint8_t v[10]; for (int k = 0; k < 10; k++) v[k] = (uint8_t)(i + k);
        if (fat16_yol_yaz(yol, v, 10) != 10) { hata |= 4; break; }
    }
    fat16_dirent_t l[64];
    int ln = fat16_listele_yol("/DYZ/ALT", l, 64), dosya = 0;
    for (int i = 0; i < ln; i++) if (l[i].name[0] != '.') dosya++;   /* "." ve ".." de listelenir */
    if (dosya != 40) hata |= 4;
    ad_yap(yol, "/DYZ/ALT/D", 39, ".DAT");
    int on = oku(yol, b, 64);
    if (on != 10 || b[0] != 39 || b[9] != 48) hata |= 4;
    if (hata & 4) { serial_write("DIZIN-YAZ-TEST:   liste="); yaz_sayi((uint64_t)(ln < 0 ? 999 : ln)); serial_write(" okunan="); yaz_sayi((uint64_t)(on < 0 ? 999 : on)); serial_write("\n"); }
    if (!temiz()) hata |= 16;

    /* 8) silme */
    if (fat16_yol_sil("/DYZ/ALT") != -2) hata |= 8;          /* dolu dizin */
    for (int i = 0; i < 40; i++) { ad_yap(yol, "/DYZ/ALT/D", i, ".DAT"); if (fat16_yol_sil(yol) != 0) hata |= 8; }
    if (fat16_yol_sil("/DYZ/ALT") != 0) hata |= 8;
    if (fat16_yol_sil("/DYZ/A.TXT") != 0 || fat16_yol_sil("/DYZ/G.LOG") != 0 || fat16_yol_sil("/DYZ") != 0) hata |= 8;
    uint32_t bos1 = fat16_bos_kume_sayisi();
    /* sizinti: silinen zincirden kalan sahipsiz kume ASENA'da KAYIP olarak
     * gorunur (bos kume sayisi karsilastirmasi kullanilmaz: UMAY ve diger
     * sinamalar ayni anda diske yazar) */
    if (!temiz()) hata |= 8 | 16;

    /* 32) TORE yazma koku (sinama kaydi: var olmayan gorev kimligi) */
    const uint32_t S = 0x7FFF0F0Fu;
    tore_set_capabilities(S, KUT_DISK_WRITE);
    tore_yazma_koku(S, "/KUT/UMAY");
    struct { const char *yol; int dizin; int bek; } K[] = {
        { "/KUT/UMAY/BENGI.TXT", 0, 1 }, { "/kut/umay/alt/x.txt", 0, 1 }, { "/KUT/UMAY", 1, 1 },
        { "/KUT", 1, 1 }, { "/KUT", 0, 0 }, { "/KUT/A.TXT", 0, 0 }, { "/UMAY.ELF", 0, 0 },
        { "/KUT/UMAYX/A.TXT", 0, 0 }, { "/KUT/UMAY/../../UMAY.ELF", 0, 0 }, { "/KUT/UMAY/./A", 0, 0 },
    };
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++)
        if (tore_yol_yazilabilir(S, K[i].yol, K[i].dizin) != K[i].bek) {
            hata |= 32; serial_write("DIZIN-YAZ-TEST:   kok beklenmedik: "); serial_write(K[i].yol); serial_write("\n");
        }
    tore_gorev_bitti(S);

    /* 64) UMAY kendi kokune yazdi mi (acilisindan sonra) */
    uint64_t boy = 0; int dz = 0;
    uint64_t t0 = timer_get_ticks();
    while ((vfs_stat("/KUT/UMAY/GUNLUK.TXT", &boy, &dz) != 0 || boy == 0) && timer_get_ticks() - t0 < 30 * hz) thread_sleep(hz / 4);
    if (vfs_stat("/KUT/UMAY/GUNLUK.TXT", &boy, &dz) != 0 || boy == 0) hata |= 64;
    serial_write("DIZIN-YAZ-TEST:   bos kume "); yaz_sayi(bos0); serial_write("->"); yaz_sayi(bos1);
    serial_write(" ekle="); yaz_sayi(top); serial_write(" liste="); yaz_sayi((uint64_t)ln);
    serial_write(" UMAY gunluk="); yaz_sayi(boy); serial_write("\n");
    {   /* sonuc satiri TEK cagrida yazilir: oteki gorevlerin satirlari araya giremez */
        static char sn[160];
        const char *on = "DIZIN-YAZ-TEST: alt dizine yaz/ekle/sil, dizin uzamasi, sizinti, ASENA, TORE yazma koku, UMAY hata=";
        const char *son = hata ? " -> HATA\n" : " -> BASARILI\n";
        int i = 0;
        while (*on) sn[i++] = *on++;
        char t[12]; int n = 0; unsigned h = (unsigned)hata;
        do { t[n++] = (char)('0' + h % 10); h /= 10; } while (h);
        while (n) sn[i++] = t[--n];
        while (*son) sn[i++] = *son++;
        sn[i] = 0;
        serial_write(sn);
    }
    kfree(a); kfree(b);
    thread_exit();
}

int sinama_umay_gunluk_durgun(uint32_t azami_sn) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    uint64_t t0 = timer_get_ticks(), deg = t0, son = (uint64_t)-1;
    while (timer_get_ticks() - t0 < (uint64_t)azami_sn * hz) {
        uint64_t boy = 0; int dz = 0;
        if (vfs_stat("/KUT/UMAY/GUNLUK.TXT", &boy, &dz) == 0 && boy > 0) {
            if (boy != son) { son = boy; deg = timer_get_ticks(); }
            else if (timer_get_ticks() - deg >= 2 * (uint64_t)hz) return 1;
        }
        thread_sleep(hz / 4 ? hz / 4 : 1);
    }
    return 0;
}

void sinama_dizin_baslat(void) { thread_create("DizinSinama", gorev); }
