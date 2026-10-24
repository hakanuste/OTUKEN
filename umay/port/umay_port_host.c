/* Ana makine (POSIX) taşıma katmanı — YALNIZ SINAMA İÇİNDİR.
 * ÖTÜKEN'e gitmez. Çekirdek mantığın doğruluğunu QEMU'suz denemeye yarar. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "umay_port.h"
#include "umay_str.h"
#ifndef UMAY_OTUKEN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

static uint64_t g_bas;
uint64_t up_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t ms = (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
    if (!g_bas) g_bas = ms;
    return ms - g_bas;
}
void up_uyu(uint32_t ms) { usleep(ms * 1000); }
void up_birak(void) { }
void up_gunluk(const char *s) { printf("%s\n", s); fflush(stdout); }
void *up_ayir(uint64_t n) { return malloc(n); }
void up_biraktir(void *p) { free(p); }

/* Sınama kolaylığı (yalnız ana makine): UMAY_KOK verilirse "/..." mutlak
 * yollar o dizinin altına bağlanır (ÖTÜKEN kökündeki /UMAY.US1 gibi). */
static const char *kok_yol(const char *yol) {
    static char t[1024];
    const char *k = getenv("UMAY_KOK");
    if (!k || !*k || !yol || yol[0] != '/') return yol;
    snprintf(t, sizeof t, "%s%s", k, yol);
    return t;
}

up_tut_t up_ac(const char *yol, uint32_t kip) {
    yol = kok_yol(yol);
    int bayrak = 0;
    if ((kip & UP_OKU) && (kip & UP_YAZ)) bayrak = O_RDWR;
    else if (kip & UP_YAZ) bayrak = O_WRONLY;
    else bayrak = O_RDONLY;
    if (kip & UP_YARAT) bayrak |= O_CREAT | (kip & UP_EKLE ? 0 : O_TRUNC);
    if (kip & UP_EKLE) bayrak |= O_APPEND;
    return (up_tut_t)open(yol, bayrak, 0644);
}
int64_t up_oku(up_tut_t t, void *b, uint64_t n) { return read(t, b, n); }
int64_t up_yaz(up_tut_t t, const void *b, uint64_t n) { return write(t, b, n); }
int up_kapat(up_tut_t t) { return close(t); }
int up_var_mi(const char *yol) { struct stat s; return stat(kok_yol(yol), &s) == 0; }
int up_dizin_yarat(const char *yol) { return mkdir(yol, 0755) == 0 ? UM_OLDU : UM_YANLIS; }
int up_yaz_yola(const char *yol, const void *v, uint64_t n) {
    int f = open(yol, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (f < 0) return -1;
    int64_t w = write(f, v, n); close(f); return (int)w;
}
int64_t up_boy(const char *yol) { struct stat s; return stat(kok_yol(yol), &s) == 0 ? (int64_t)s.st_size : -1; }

/* IPC: sınamada bellek içi tek kuyruk */
static char g_kuyruk[8][UMAY_ILETI_MAX];
static int  g_bas_i, g_son_i, g_sayi_i;
up_tut_t up_kapi_ac(const char *ad) { (void)ad; return 100; }
/* Ana bilgisayarda gercek bir servis YOKTUR. Eskiden her ada 100
 * donuyordu; bu SAHTE BASARI idi ve IPC motoru baglandigini saniyordu.
 * Yalnizca testin actigi "yerel" kapi taninir, gerisi gecersizdir. */
up_tut_t up_kapi_bagla(const char *ad) {
    if (ad && ad[0] == 'y' && ad[1] == 'e' && ad[2] == 'r') return 100;
    return UP_GECERSIZ;
}
int up_gonder(up_tut_t t, const void *v, uint32_t n) {
    (void)t;
    if (g_sayi_i >= 8) return UM_DOLU;
    uint32_t k = n < UMAY_ILETI_MAX - 1 ? n : UMAY_ILETI_MAX - 1;
    memcpy(g_kuyruk[g_son_i], v, k); g_kuyruk[g_son_i][k] = 0;
    g_son_i = (g_son_i + 1) % 8; g_sayi_i++;
    return (int)k;
}
/* Ana makinede tek geri dongu kuyrugu var: yanit ayni kapiya doner. */
up_tut_t up_yanit_kapisi(up_tut_t kendi) { return kendi; }

int up_al(up_tut_t t, void *v, uint32_t n, uint32_t bekle) {
    (void)t; (void)bekle;
    if (g_sayi_i == 0) return 0;
    uint32_t k = (uint32_t)strlen(g_kuyruk[g_bas_i]);
    if (k > n) k = n;
    memcpy(v, g_kuyruk[g_bas_i], k);
    g_bas_i = (g_bas_i + 1) % 8; g_sayi_i--;
    return (int)k;
}

int up_surec_listele(up_surec_t *d, int en_cok) {
    static const char *ADLAR[] = { "cekirdek", "kabuk", "umay", "ulak" };
    int n = 4 < en_cok ? 4 : en_cok;
    for (int i = 0; i < n; i++) {
        d[i].pid = i + 1;
        um_kopya(d[i].ad, sizeof d[i].ad, ADLAR[i]);
        d[i].bellek_kb = 512 * (uint64_t)(i + 1);
        d[i].durum = 0;
    }
    return n;
}
static uint32_t g_yetki = YET_DOSYA_OKU | YET_DOSYA_YAZ | YET_SUREC_GOR;
void host_yetki_ayarla(uint32_t y) { g_yetki = y; }
int up_yetkim_var_mi(up_yetki_t y) { return (g_yetki & (uint32_t)y) != 0; }

/* Ana makinede gerçek motor yok; US katmanı stub'a düşer. */
struct umay_us *up_us_motoru(void) { return 0; }

/* ÜLGEN yerine sınamada başsız (headless) pencere */
struct up_pencere { int en, boy; };
static struct up_pencere g_p;
static int g_gui_acik;
up_pencere_t *up_pencere_yarat(const char *b, int en, int boy) {
    (void)b;
    if (!getenv("UMAY_GUI")) return 0;
    g_p.en = en; g_p.boy = boy; g_gui_acik = 1;
    return &g_p;
}
void up_pencere_yok(up_pencere_t *p) { (void)p; g_gui_acik = 0; }
void up_dikdortgen(up_pencere_t *p, int x, int y, int en, int boy, uint32_t r) {
    (void)p;(void)x;(void)y;(void)en;(void)boy;(void)r;
}
void up_yazi(up_pencere_t *p, int x, int y, const char *s, uint32_t r) {
    (void)p;(void)x;(void)y;(void)s;(void)r;
}
void up_sun(up_pencere_t *p) { (void)p; }
int up_olay_al(up_pencere_t *p, up_olay_t *o) { (void)p;(void)o; return 0; }
int up_metin_en(const char *s) { return (int)um_uzun(s) * 8; }
#endif

/* Ana bilgisayarda ses yok: ezgi gunluge yazilir, sessiz gecilir. */
void up_ozan_soyle(const char *metin, int hiz) { (void)hiz; if (metin) up_gunluk(metin); }
void up_ses_nota(uint32_t hz, uint32_t ms) { (void)hz; (void)ms; }

/* Ana bilgisayarda ses aygiti yok. */
int up_dede_konus(const char *metin) { (void)metin; return -1; }
int up_ses_pcm(const int16_t *pcm, uint32_t kare) { (void)pcm; (void)kare; return -1; }   /* ana makinede ses aygıtı yok */
