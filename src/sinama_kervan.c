/* =====================================================================
 * 1.4.9 — KERVAN-TEST (yalniz TANI; sinama diskinde K2IYI.KRV varsa)
 * Paketler tests/kervan/hazirla.py'den (her kosuda yeni icerik). Denetimler:
 *     1 liste: iyi paketler GECERLI, bozuklar GECERSIZ ve dogru nedenle
 *     2 uretim anahtariyla imzali ORNEK.KRV gecerli (uretim anahtari calisiyor)
 *     4 kurulum: dosyalar diskte, SHA-256'lari beklenenle AYNI
 *     8 yukseltme: degisen dosya yeni, yeni surumde olmayan dosya SILINDI
 *    16 kaldirma: dosyalar yok, bos kume sayisi kurulum ONCESIYLE AYNI (sizinti yok)
 *    32 reddedilenler DISKE HICBIR SEY yazmadi (bos kume sayisi degismedi)
 *    64 kayit kalici: /KERVAN.LST yeniden okununca kurulu paket goruluyor
 *   128 arayuz: rihtimdaki KERVAN'dan (gercek tiklama) paket kuruldu
 * ===================================================================== */
#include "sinama_kervan.h"
#include "kervan2.h"
#include "uygulama.h"
#include "fat16.h"
#include "vfs.h"
#include "heap.h"
#include "thread.h"
#include "timer.h"
#include "serial.h"
#include "gui.h"
#include "ulgen.h"
#include "ulgen_theme.h"
#include "bearssl.h"
#include "sinama148.h"

#define BEKLE(kosul, tik) do { uint64_t _t0 = timer_get_ticks(); \
    while (!(kosul) && timer_get_ticks() - _t0 < (tik)) thread_sleep(1); } while (0)

static int esit(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int icerir(const char *s, const char *p) {
    for (; *s; s++) { const char *a = s, *b = p; while (*b && *a == *b) { a++; b++; } if (!*b) return 1; }
    return 0;
}
static void yaz_sayi(uint64_t v) {
    char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char c[24]; int k = 0; while (n) c[k++] = t[--n]; c[k] = 0; serial_write(c);
}

/* dosyanın SHA-256'sı, onaltılı; yoksa -1 */
static int dosya_ozet(const char *ad, char *hex, uint32_t *boy) {
    char yol[16]; yol[0] = '/'; int i = 0; while (ad[i] && i < 14) { yol[i + 1] = ad[i]; i++; } yol[i + 1] = 0;
    int fd = vfs_open(yol);
    if (fd < 0) return -1;
    static uint8_t t[4096];
    br_sha256_context h; br_sha256_init(&h);
    uint32_t n = 0;
    for (;;) { int r = vfs_read(fd, t, sizeof t); if (r <= 0) break; br_sha256_update(&h, t, (size_t)r); n += (uint32_t)r; }
    vfs_close(fd);
    uint8_t o[32]; br_sha256_out(&h, o);
    for (int k = 0; k < 32; k++) { hex[2 * k] = "0123456789abcdef"[o[k] >> 4]; hex[2 * k + 1] = "0123456789abcdef"[o[k] & 15]; }
    hex[64] = 0;
    *boy = n;
    return 0;
}

/* BEKLENEN.TXT: "v1 AD BOY OZET" satırları; surum ('1'/'2') dosyalarını denetle */
static int icerik_denetle(char surum, int *adet) {
    static char m[2048];
    int fd = vfs_open("/BEKLENEN.TXT");
    if (fd < 0) return -1;
    int n = vfs_read(fd, m, sizeof m - 1);
    vfs_close(fd);
    if (n <= 0) return -1;
    m[n] = 0;
    int hata = 0; *adet = 0;
    char *p = m;
    while (*p) {
        char *s = p; while (*p && *p != '\n') p++; if (*p) *p++ = 0;
        if (s[0] != 'v' || s[1] != surum || s[2] != ' ') continue;
        char *ad = s + 3; char *q = ad; while (*q && *q != ' ') q++; *q++ = 0;
        uint32_t boy = 0; while (*q >= '0' && *q <= '9') boy = boy * 10 + (uint32_t)(*q++ - '0');
        q++;
        char hex[65]; uint32_t gboy;
        if (dosya_ozet(ad, hex, &gboy) != 0 || gboy != boy || !esit(hex, q)) {
            serial_write("KERVAN-TEST:   icerik farkli: "); serial_write(ad); serial_write("\n");
            hata++;
        }
        (*adet)++;
    }
    return hata;
}

static int dosya_var(const char *ad) {
    char yol[16]; yol[0] = '/'; int i = 0; while (ad[i] && i < 14) { yol[i + 1] = ad[i]; i++; } yol[i + 1] = 0;
    int fd = vfs_open(yol); if (fd < 0) return 0; vfs_close(fd); return 1;
}

static kervan2_oge_t *bul(kervan2_oge_t *l, int n, const char *dosya) {
    for (int i = 0; i < n; i++) if (esit(l[i].dosya, dosya)) return &l[i];
    return 0;
}

static void t_tikla(int x, int y) { gui_fare_isle(x, y, 1); thread_sleep(1); gui_fare_isle(x, y, 0); }

static volatile int sk_bitti = 0;
int sinama_kervan_bitti(void) { return __atomic_load_n(&sk_bitti, __ATOMIC_ACQUIRE); }
static void bitir(void) { __atomic_store_n(&sk_bitti, 1, __ATOMIC_RELEASE); thread_exit(); }

static void gorev(void) {
    if (!dosya_var("K2IYI.KRV")) {
        serial_write("KERVAN-TEST: atlandi (sinama diskinde K2IYI.KRV yok; bkz. tests/kervan)\n");
        bitir();
    }
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    int hata = 0;
    char neden[64];
    static kervan2_oge_t l[24];
    int n = kervan2_liste(l, 24);

    /* 1) liste ve nedenler */
    struct { const char *dosya; int durum; const char *neden; } B[] = {
        {"K2IYI.KRV", KERVAN2_GECERLI, 0}, {"K2IYI2.KRV", KERVAN2_GECERLI, 0},
        {"K2BOZUK.KRV", KERVAN2_GECERSIZ, "IMZA GECERSIZ"}, {"K2YABAN.KRV", KERVAN2_GECERSIZ, "IMZA GECERSIZ"},
        {"K2ANAH0.KRV", KERVAN2_GECERSIZ, "IMZA GECERSIZ"}, {"K2ANAH7.KRV", KERVAN2_GECERSIZ, "taninmiyor"},
        {"K2OZET.KRV", KERVAN2_GECERSIZ, "ozeti tutmuyor"},
        {"K2KORUN.KRV", KERVAN2_GECERLI, 0}, {"K2CAKIS.KRV", KERVAN2_GECERLI, 0},
    };
    for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++) {
        kervan2_oge_t *o = bul(l, n, B[i].dosya);
        int ok = o && o->durum == B[i].durum && (!B[i].neden || icerir(o->neden, B[i].neden));
        if (!ok) {
            hata |= 1;
            serial_write("KERVAN-TEST:   liste beklenmedik: "); serial_write(B[i].dosya);
            serial_write(o ? " neden=" : " (yok)"); if (o) serial_write(o->neden); serial_write("\n");
        }
    }
    kervan2_oge_t *orn = bul(l, n, "ORNEK.KRV");
    if (!orn || orn->durum != KERVAN2_GECERLI || !esit(orn->ad, "ornek-belgeler")) hata |= 2;

    /* 6) reddedilenler: hiçbiri diske yazmamalı */
    uint32_t bos0 = fat16_bos_kume_sayisi();
    const char *red[] = {"K2BOZUK.KRV", "K2YABAN.KRV", "K2ANAH0.KRV", "K2ANAH7.KRV", "K2OZET.KRV", "K2KORUN.KRV", "K2CAKIS.KRV"};
    for (unsigned i = 0; i < sizeof red / sizeof red[0]; i++) {
        if (kervan2_kur(red[i], neden) == 0) { hata |= 32; serial_write("KERVAN-TEST:   KURULMAMALIYDI: "); serial_write(red[i]); serial_write("\n"); }
        else { serial_write("KERVAN-TEST:   reddedildi "); serial_write(red[i]); serial_write(": "); serial_write(neden); serial_write("\n"); }
    }
    uint32_t umay_boy; char umay_hex[65];
    if (dosya_ozet("UMAY.ELF", umay_hex, &umay_boy) != 0 || umay_boy < 1000) hata |= 32;   /* UMAY.ELF bozulmadı */
    if (fat16_bos_kume_sayisi() != bos0) hata |= 32;

    /* 3) kurulum ve içerik */
    int adet = 0;
    if (kervan2_kur("K2IYI.KRV", neden) != 0) { hata |= 4; serial_write("KERVAN-TEST:   kurulamadi: "); serial_write(neden); serial_write("\n"); }
    int ih = icerik_denetle('1', &adet);
    if (ih != 0 || adet != 3) hata |= 4;
    char sur[16];
    if (!kervan2_kurulu_mu("sinama-iyi", sur) || !esit(sur, "1.0.0")) hata |= 4;

    /* 4) yükseltme */
    if (kervan2_kur("K2IYI2.KRV", neden) != 0) hata |= 8;
    ih = icerik_denetle('2', &adet);
    if (ih != 0 || adet != 2 || dosya_var("IYI3.DAT")) hata |= 8;
    if (!kervan2_kurulu_mu("sinama-iyi", sur) || !esit(sur, "2.0.0")) hata |= 8;

    /* 7) kalıcılık: kayıt yeniden okunur */
    kervan2_init();
    if (!kervan2_kurulu_mu("sinama-iyi", sur) || !esit(sur, "2.0.0")) hata |= 64;

    /* 5) kaldırma, sızıntı */
    if (kervan2_kaldir("sinama-iyi", neden) != 0) hata |= 16;
    if (dosya_var("IYI1.TXT") || dosya_var("IYI2.TXT") || kervan2_kurulu_mu("sinama-iyi", 0)) hata |= 16;
    uint32_t bos1 = fat16_bos_kume_sayisi();
    /* KERVAN.LST ilk kurulumda yaratıldı: o dosyanın kümesi dışında her şey geri dönmeli */
    uint32_t lst_boy = 0; char lst_hex[65];
    uint32_t lst_kume = (dosya_ozet("KERVAN.LST", lst_hex, &lst_boy) == 0) ? (lst_boy + 511) / 512 : 0;
    if (lst_kume == 0) lst_kume = 1;
    if (bos1 + lst_kume != bos0) hata |= 16;

    /* 8) arayüz: rıhtımdaki KERVAN (tamga 11), K2IYI satırı, "Kur / Yükselt" */
    BEKLE(sinama148_bitti(), 120 * hz);
    int rx = -1, ry = -1;
    for (int k = 0; k < 7 && rx < 0; k++) {
        int x, y, b;
        if (ulgen_theme_rihtim_konum(k, &x, &y, &b) < 0) continue;
        if (ulgen_theme_dock_hit(x + b / 2, y + b / 2) == 11) { rx = x + b / 2; ry = y + b / 2; }
    }
    if (rx >= 0 && ulgen_noktadaki_yuzey(rx, ry) < 0) t_tikla(rx, ry);
    BEKLE(uygulama_penceresi(UYG_KERVAN) >= 0, 10 * hz);
    int id = uygulama_penceresi(UYG_KERVAN);
    n = kervan2_liste(l, 24);
    int sira = -1;
    for (int i = 0; i < n; i++) if (esit(l[i].dosya, "K2IYI.KRV")) sira = i;
    const ulgen_yuzey_t *ys = id >= 0 ? ulgen_yuzey_al(id) : 0;
    if (ys && sira >= 0 && sira < (470 - 38 - 100) / 44) {
        thread_sleep(20);
        t_tikla(ys->x + 60, ys->y + 38 + sira * 44 + 10);       /* satır */
        thread_sleep(10);
        t_tikla(ys->x + 40, ys->y + 470 - 100 + 8 + 10);         /* Kur / Yükselt (ALT=100) */
        BEKLE(kervan2_kurulu_mu("sinama-iyi", sur), 10 * hz);
    }
    if (!kervan2_kurulu_mu("sinama-iyi", sur) || !esit(sur, "1.0.0")) hata |= 128;
    if (id >= 0 && ys) {
        int kx = ys->x + ys->w - 10, ky = ys->y - ULGEN_BASLIK_YUK / 2;
        if (ulgen_kapat_dugmesi_mi(id, kx, ky)) t_tikla(kx, ky);
    }
    serial_write("KERVAN-TEST: imzali paket: liste, red, kur, yukselt, kaldir, sizinti, kalicilik, arayuz (bos kume ");
    yaz_sayi(bos0); serial_write("->"); yaz_sayi(bos1); serial_write("+LST "); yaz_sayi(lst_kume);
    serial_write(") hata="); yaz_sayi((uint64_t)hata);
    serial_write(hata ? " -> HATA\n" : " -> BASARILI\n");
    bitir();
}

void sinama_kervan_baslat(void) { thread_create("KervanSinama", gorev); }
