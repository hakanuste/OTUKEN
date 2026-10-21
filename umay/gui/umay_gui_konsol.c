/* =====================================================================
 * KONSOL ÖN YÜZÜ — UMAY'ın ana bilgisayarda tek başına çalışan yüzü.
 *
 * ÜLGEN pencere katmanının (gui/umay_ulgen.c) yerine geçer: aynı
 * umay_gui.h sözleşmesini uygular ama uçbirime yazar, stdin'den okur.
 * UMAY'ın çekirdeği (OTAĞ, ALP, OZAN, BİLGİ, US) hiç değişmez; yalnız
 * konuşmanın geçtiği yüzey değişir.
 *
 * GİRDİ: satır satır stdin. Bloklamaz — UMAY'ın tur döngüsü dönmeye
 * devam eder, yoksa otağ görevleri ilerlemez.
 *
 * ÇIKTI: konuşma akışı ve durum satırı. Renk, uçbirim destekliyorsa
 * kullanılır (TERM tanımlı ve çıktı bir uçbirime gidiyorsa).
 * ===================================================================== */
#include "umay_gui.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int  g_acik = 0;
static int  g_kapat_istendi = 0;
static int  g_renk = 0;
static char g_durum[128];

/* --- renk yardımcıları --- */
static const char *R_TURKUAZ = "\033[38;5;44m";
static const char *R_LAVANTA = "\033[38;5;141m";
static const char *R_KREM    = "\033[38;5;223m";
static const char *R_GRI     = "\033[38;5;245m";
static const char *R_SIFIR   = "\033[0m";

/* printf'in %-14s hizalaması BAYT sayar; Türkçe harfler çok baytlı
 * olduğu için sütunlar kayıyordu. Burada KARAKTER sayılır. */
static void ad_yaz(const char *s, int genislik)
{
    int kar = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) kar++;        /* devam baytlarını sayma */
    printf("%s", s);
    for (int i = kar; i < genislik; i++) putchar(' ');
}

/* ALP durum adi: ULGEN on yuzundeki yardimci statiktir, paylasilmaz. */
static const char *alp_durum_adi(alp_durum_t d)
{
    switch (d) {
    case ALP_BOS:      return "boş";
    case ALP_KOSUYOR:  return "koşuyor";
    case ALP_BEKLIYOR: return "bekliyor";
    case ALP_BITTI:    return "bitti";
    default:           return "yanlış";
    }
}

static const char *renk(const char *k) { return g_renk ? k : ""; }
static const char *sifir(void)          { return g_renk ? R_SIFIR : ""; }

int gui_ac(int en, int boy)
{
    (void)en; (void)boy;

    /* Renk yalnız gerçek bir uçbirime yazarken: boruya ya da dosyaya
     * kaçış dizisi dökmek çıktıyı bozar. */
    const char *term = getenv("TERM");
    g_renk = isatty(STDOUT_FILENO) && term && !um_esit(term, "dumb");

    /* stdin bloklamasın: UMAY'ın turu girdi beklerken durmamalı. */
    int bayrak = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (bayrak != -1) fcntl(STDIN_FILENO, F_SETFL, bayrak | O_NONBLOCK);

    g_acik = 1;
    g_kapat_istendi = 0;
    g_durum[0] = 0;

    printf("\n%s╔══════════════════════════════════════════════╗%s\n",
           renk(R_TURKUAZ), sifir());
    printf("%s║%s  UMAY — Dede Korkut dili ile                 %s║%s\n",
           renk(R_TURKUAZ), renk(R_KREM), renk(R_TURKUAZ), sifir());
    printf("%s╚══════════════════════════════════════════════╝%s\n",
           renk(R_TURKUAZ), sifir());
    printf("%s  çıkmak için: cik   ·   yardım için: yardim%s\n\n",
           renk(R_GRI), sifir());
    fflush(stdout);
    return UM_OLDU;
}

int gui_acik_mi(void) { return g_acik; }

int gui_konsolu_sahiplenir(void) { return 1; }

void gui_kapat(void)
{
    if (!g_acik) return;
    g_acik = 0;
    printf("\n%s  — ocak söndü —%s\n\n", renk(R_GRI), sifir());
    fflush(stdout);
}

int gui_kapat_istendi(void) { return g_kapat_istendi; }

void gui_satir_ekle(const char *kim, const char *metin)
{
    if (!g_acik || !kim || !metin) return;
    /* Kullanıcının kendi sözünü geri basmayız: uçbirim zaten gösterdi. */
    if (um_esit(kim, "SEN")) return;
    printf("%s%s%s  %s\n", renk(R_LAVANTA), kim, sifir(), metin);
    fflush(stdout);
}

void gui_durum(const char *metin)
{
    if (!g_acik || !metin) return;
    /* Aynı durumu tekrar tekrar basmayalım; akışı boğar. */
    if (um_esit(g_durum, metin)) return;
    um_kopya(g_durum, sizeof g_durum, metin);
    printf("%s  · %s%s\n", renk(R_GRI), metin, sifir());
    fflush(stdout);
}

void gui_ciz(const otag_t *o)
{
    /* Konsolda sürekli otağ tablosu basmak akışı okunmaz kılar.
     * Otağ görünümü istek üzerine ("otag" buyruğu) yazdırılır. */
    (void)o;
}

/* Otağ görünümünü İSTEK ÜZERİNE basar; ana döngü "otag" buyruğunda
 * çağırır. Sürekli basmak uçbirimdeki konuşma akışını boğardı. */
void gui_otag_yaz(const otag_t *o)
{
    if (!o) return;
    printf("\n%s  OTAĞ — ajan meclisi%s\n", renk(R_TURKUAZ), sifir());
    for (int i = 0; i < o->alp_sayisi; i++) {
        const alp_t *a = o->alplar[i];
        if (!a) continue;
        printf("    ");
        ad_yaz(a->ad, 14);
        printf("%s%s%s\n", renk(a->durum == ALP_KOSUYOR ? R_TURKUAZ : R_GRI),
               alp_durum_adi(a->durum), sifir());
    }
    printf("%s    kuyruk: %d   biten: %u%s\n\n",
           renk(R_GRI), otag_bekleyen_sayi(o), o->bitmis, sifir());
    fflush(stdout);
}

int gui_giris_yokla(umay_ileti_t *i)
{
    if (!g_acik || !i) return 0;

    static char tampon[UMAY_ILETI_MAX];
    static uint32_t o = 0;

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            if (o == 0) continue;
            tampon[o] = 0;
            o = 0;

            /* Uçbirime özgü buyruklar: UMAY'a iletilmez. */
            if (um_esit(tampon, "cik") || um_esit(tampon, "çık") ||
                um_esit(tampon, "quit") || um_esit(tampon, "exit")) {
                g_kapat_istendi = 1;
                return 0;
            }

            i->tur     = KANAL_KLAVYE;
            i->damga   = up_ms();
            i->kaynak  = 1;
            i->uzunluk = um_kopya(i->govde, sizeof i->govde, tampon);
            return 1;
        }
        if (o + 1 < sizeof tampon) tampon[o++] = c;
    }

    /* EOF (Ctrl-D ya da boru kapandı): kapanma isteği sayılır.
     * read 0 döndüğünde döngü biter; bunu burada ayırt ediyoruz. */
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != 0) {
        g_kapat_istendi = 1;
    }
    return 0;
}
