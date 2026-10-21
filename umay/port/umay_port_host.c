/* Ana makine (POSIX) taşıma katmanı — YALNIZ SINAMA İÇİNDİR.
 * ÖTÜKEN'e gitmez. Çekirdek mantığın doğruluğunu QEMU'suz denemeye yarar. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "umay_port.h"
#include "umay_piper.h"
#include "umay_us.h"
#include "umay_str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
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

up_tut_t up_ac(const char *yol, uint32_t kip) {
    /* ÖTÜKEN'de konsol bir aygıt dosyasıdır; ana bilgisayarda stdin'dir.
     * Kanal katmanı bu yolu açmaya çalışır, orada dosya yoktur. */
    if (yol && um_esit(yol, "/aygit/konsol")) return 0;
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
int up_var_mi(const char *yol) { struct stat s; return stat(yol, &s) == 0; }
int up_dizin_yarat(const char *yol) { return mkdir(yol, 0755) == 0 ? UM_OLDU : UM_YANLIS; }
int up_yaz_yola(const char *yol, const void *v, uint64_t n) {
    int f = open(yol, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (f < 0) return -1;
    int64_t w = write(f, v, n); close(f); return (int)w;
}
int64_t up_boy(const char *yol) { struct stat s; return stat(yol, &s) == 0 ? (int64_t)s.st_size : -1; }

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
/* Ana bilgisayarda da GERÇEK motor kullanılır: US1 modeli dosyadan
 * okunur. Model yoksa ya da bozuksa motor BAŞARISIZ döner ve US
 * katmanı sırayla IPC'ye, sonra stub'a düşer — sahte başarı yok. */
/* --- US motoru secimi (ana bilgisayar) ---
 * Once OLLAMA denenir, tutmazsa yerel US1'e dusulur. Secim PORT'ta
 * yapilir; cekirdek hangi motorun kosacagini bilmez ve icinde tek bir
 * kosullu derleme tasimaz.
 *
 * NEDEN SARMALAYICI: us_kur() PORT'tan TEK bir motor alir. Ollama'yi
 * dogrudan dondurmek, Ollama yokken US1'i de kaybettirirdi. */
static umay_us_t *g_secilen;

static int konak_hazirla(umay_us_t *u, const char *model_yolu)
{
    umay_us_t *adaylar[2];
    int n = 0;
    adaylar[n++] = us_ollama_motor();
    adaylar[n++] = (umay_us_t *)us_yerel_motor();

    for (int i = 0; i < n; i++) {
        umay_us_t *m = adaylar[i];
        if (!m) continue;
        if (!m->hazirla || m->hazirla(m, model_yolu) == UM_OLDU) {
            g_secilen = m;
            u->ad = m->ad;              /* gunlukte GERCEK ad gorunsun */
            u->yetenek = m->yetenek;
            return UM_OLDU;
        }
    }
    return UM_YOK;
}

static int konak_uret(umay_us_t *u, const us_istem_t *i, char *c, uint32_t n)
{
    (void)u;
    if (!g_secilen || !g_secilen->uret) return UM_YOK;
    return g_secilen->uret(g_secilen, i, c, n);
}

static void konak_kapat(umay_us_t *u)
{
    (void)u;
    if (g_secilen && g_secilen->kapat) g_secilen->kapat(g_secilen);
    g_secilen = 0;
}

static umay_us_t g_konak = {
    .ad = "konak-us", .yetenek = 0,
    .hazirla = konak_hazirla, .uret = konak_uret, .kapat = konak_kapat, .ic = 0
};

struct umay_us *up_us_motoru(void) { return (struct umay_us *)&g_konak; }

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

/* Ana bilgisayarda ses yok: ezgi gunluge yazilir, sessiz gecilir. */
/* Ses yoksa metni günlüğe basmak KONUŞMA DEĞİLDİR; yalnız yankı
 * yapar ve yanıtı ikinci kez yazdırır. Sessiz geçilir. */
void up_ozan_soyle(const char *metin, int hiz) { (void)metin; (void)hiz; }
void up_ses_nota(uint32_t hz, uint32_t ms) { (void)hz; (void)ms; }

/* --- Gerçek konuşma: PIPER ---
 * Ana bilgisayarda ses, sinir ağı tabanlı Piper ile üretilir
 * (ses/umay_piper.c). Piper ya da ses modeli ya da çalıcı yoksa
 * up_ses_var_mi() SIFIR döner, sürücü takılmaz ve UMAY yalnız
 * yazıyla yanıtlar — sahte başarı yok. */
/* --- Kütüphane kökü ---
 * Tek başına çalışan bir programın verisini nerede bulacağı belli
 * olmalı. Sıra: UMAY_KUT, çalışma dizinindeki kut/, kullanıcının
 * ~/.local/share/umay/kut, sistem geneli /usr/local/share/umay/kut. */
/* Bir dizin GERÇEKTEN kütüphane mi? Model dosyası ya da ortak havuz
 * içinde duruyorsa evet. */
static int kut_mu(const char *d)
{
    struct stat st;
    char y[512];
    if (!d || !*d) return 0;
    if (stat(d, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    snprintf(y, sizeof y, "%s/umay.us1", d);
    if (stat(y, &st) == 0) return 1;
    snprintf(y, sizeof y, "%s/ortak", d);
    if (stat(y, &st) == 0) return 1;
    return 0;
}

int up_dizin_listele(const char *yol, up_girdi_t *g, int baslangic, int en_cok)
{
    if (!yol || !g || en_cok <= 0 || baslangic < 0) return -1;
    DIR *d = opendir(yol);
    if (!d) return -1;
    int n = 0, gorulen = 0;
    struct dirent *e;
    while (n < en_cok && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;          /* . .. gizliler */
        if (gorulen++ < baslangic) continue;        /* sayfalama */
        /* ABI'deki ad alanı 32 bayt. Uzun adı KIRPMAK yanıltıcı olur:
         * kırpılmış adla dosya açılamaz, belge sessizce kaybolur.
         * Böyle bir girdi atlanır ve sorun görünür kalır. */
        if (strlen(e->d_name) >= sizeof g[n].ad) continue;
        char tam[1024];
        snprintf(tam, sizeof tam, "%s/%s", yol, e->d_name);
        struct stat st;
        if (stat(tam, &st) != 0) continue;
        memcpy(g[n].ad, e->d_name, strlen(e->d_name) + 1);
        g[n].boy = (uint64_t)st.st_size;
        g[n].dizin_mi = S_ISDIR(st.st_mode) ? 1u : 0u;
        g[n].ayrilmis = 0;
        n++;
    }
    closedir(d);
    return n;
}

const char *up_kut_kok(void)
{
    static char yol[512];
    static int bakildi;
    if (bakildi) return yol;
    bakildi = 1;

    const char *d = getenv("UMAY_KUT");
    if (d && *d) { snprintf(yol, sizeof yol, "%s", d); return yol; }

    /* Aday DİZİN OLMASI yetmez, İÇİNDE KÜTÜPHANE OLMALI.
     * Yalnız varlığa bakmak yanıltıcıydı: çalışma dizininde önceki
     * bir koşudan kalma boş bir "kut" klasörü, düzgün kurulumu
     * gölgeliyor ve program sessizce stub motora düşüyordu. */
    struct stat st;
    if (kut_mu("kut")) { snprintf(yol, sizeof yol, "kut"); return yol; }
    /* Kurulumda gömülen yer. "make kur ONEK=..." bunu tanımlar;
     * yoksa program özel bir dizine kurulduğunda verisini bulamıyor
     * ve sessizce stub motora düşüyordu. */
#ifdef UMAY_KUT_ONEK
    snprintf(yol, sizeof yol, "%s", UMAY_KUT_ONEK);
    if (kut_mu(yol)) return yol;
#endif
    const char *ev = getenv("HOME");
    if (ev) {
        snprintf(yol, sizeof yol, "%s/.local/share/umay/kut", ev);
        if (kut_mu(yol)) return yol;
    }
    snprintf(yol, sizeof yol, "/usr/local/share/umay/kut");
    if (kut_mu(yol)) return yol;

    (void)st;
    snprintf(yol, sizeof yol, "kut");    /* hiçbiri yoksa: göreli */
    return yol;
}

void up_kut_dosya(const char *ad, char *cik, uint32_t n)
{
    snprintf(cik, n, "%s/%s", up_kut_kok(), ad ? ad : "");
}

int up_ses_var_mi(void) { return piper_var(); }
int up_dede_konus(const char *metin) { return piper_konus(metin); }
