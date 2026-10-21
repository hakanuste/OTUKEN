/* BENGİ — türlenmiş kalıcı hafıza. VFS üstünde düz satır deposu. */
#include "umay_bengi.h"
#include "umay_port.h"
#include "umay_str.h"

static bengi_kayit_t g_bengi[UMAY_BENGI_MAX];
static int  g_sayi;
static char g_yol[UMAY_YOL_MAX];
static int  g_kirli;

const char *bengi_tur_adi(bengi_tur_t t) {
    switch (t) {
    case BENGI_KULLANICI: return "kullanici";
    case BENGI_PROJE:     return "proje";
    case BENGI_KARAR:     return "karar";
    default:              return "sonuc";
    }
}
static bengi_tur_t tur_coz(const char *s) {
    if (um_esit(s, "proje")) return BENGI_PROJE;
    if (um_esit(s, "karar")) return BENGI_KARAR;
    if (um_esit(s, "sonuc")) return BENGI_SONUC;
    return BENGI_KULLANICI;
}

int bengi_sayi(void) { return g_sayi; }

static int bul(const char *a) {
    for (int i = 0; i < g_sayi; i++)
        if (g_bengi[i].dolu && um_esit(g_bengi[i].anahtar, a)) return i;
    return -1;
}

/* Havuz dolduğunda en eski, doğrulanmamış SONUC kaydı düşer;
 * KULLANICI ve KARAR kayıtları hiçbir zaman kendiliğinden silinmez. */
static int yer_ac(void) {
    int aday = -1;
    uint64_t en_eski = 0;
    for (int i = 0; i < g_sayi; i++) {
        if (!g_bengi[i].dolu) return i;
        if (g_bengi[i].tur != BENGI_SONUC || g_bengi[i].dogrulandi) continue;
        if (aday < 0 || g_bengi[i].damga < en_eski) { aday = i; en_eski = g_bengi[i].damga; }
    }
    return aday;
}

int bengi_kaydet(bengi_tur_t tur, const char *anahtar, const char *deger, int dogrulandi) {
    if (!anahtar || !*anahtar) return UM_YANLIS;
    if (tur >= BENGI_TUR_SAYI) return UM_YANLIS;
    /* dogrulandi zorunlu bir bayraktır: 0 ya da 1 dışında değer kabul edilmez */
    if (dogrulandi != 0 && dogrulandi != 1) return UM_YANLIS;
    int i = bul(anahtar);
    if (i < 0) {
        if (g_sayi < UMAY_BENGI_MAX) i = g_sayi++;
        else { i = yer_ac(); if (i < 0) return UM_DOLU; }
        um_bellek_doldur(&g_bengi[i], 0, sizeof g_bengi[i]);
        um_kopya(g_bengi[i].anahtar, sizeof g_bengi[i].anahtar, anahtar);
        g_bengi[i].dolu = 1;
    }
    g_bengi[i].tur = tur;
    g_bengi[i].dogrulandi = dogrulandi ? 1 : 0;
    g_bengi[i].damga = up_ms();
    um_kopya(g_bengi[i].deger, sizeof g_bengi[i].deger, deger ? deger : "");
    g_kirli = 1;
    return UM_OLDU;
}

int bengi_yaz(const char *anahtar, const char *deger) {
    return bengi_kaydet(BENGI_KULLANICI, anahtar, deger, 1);
}

const char *bengi_oku(const char *anahtar) {
    int i = bul(anahtar);
    return i < 0 ? 0 : g_bengi[i].deger;
}

int bengi_dogrula(const char *anahtar) {
    int i = bul(anahtar);
    if (i < 0) return UM_YOK;
    g_bengi[i].dogrulandi = 1;
    g_kirli = 1;
    return UM_OLDU;
}

int bengi_sil(const char *anahtar) {
    int i = bul(anahtar);
    if (i < 0) return UM_YOK;
    g_bengi[i].dolu = 0;
    g_kirli = 1;
    return UM_OLDU;
}

int bengi_ara(const char *kelime, const char **cikan, int en_cok) {
    int n = 0;
    for (int i = 0; i < g_sayi && n < en_cok; i++)
        if (g_bengi[i].dolu &&
            (um_icinde(g_bengi[i].deger, kelime) || um_icinde(g_bengi[i].anahtar, kelime)))
            cikan[n++] = g_bengi[i].deger;
    return n;
}

int bengi_turu_ara(bengi_tur_t tur, const bengi_kayit_t **cikan, int en_cok) {
    int n = 0;
    for (int i = 0; i < g_sayi && n < en_cok; i++)
        if (g_bengi[i].dolu && g_bengi[i].tur == tur) cikan[n++] = &g_bengi[i];
    return n;
}

void bengi_ozet(char *c, uint32_t n) {
    um_kopya(c, n, "");
    for (int i = 0; i < g_sayi && um_uzun(c) + 96 < n; i++) {
        if (!g_bengi[i].dolu) continue;
        char s[256];
        um_bicim(s, sizeof s, "[%s%s] %s: %s\n",
                 bengi_tur_adi(g_bengi[i].tur),
                 g_bengi[i].dogrulandi ? "" : "?",
                 g_bengi[i].anahtar, g_bengi[i].deger);
        um_ekle(c, n, s);
    }
}

/* US'e verilecek özet: yalnız doğrulanmış KULLANICI/PROJE/KARAR kayıtları.
 * Doğrulanmamış görev çıktıları modele dayanak olarak sunulmaz. */
void bengi_baglam(char *c, uint32_t n) {
    um_kopya(c, n, "");
    for (int i = 0; i < g_sayi && um_uzun(c) + 96 < n; i++) {
        const bengi_kayit_t *k = &g_bengi[i];
        if (!k->dolu || !k->dogrulandi) continue;
        if (k->tur == BENGI_SONUC) continue;
        um_ekle(c, n, k->deger);
        um_ekle(c, n, "\n");
    }
}

int bengi_kur(const char *yol) {
    g_sayi = 0; g_kirli = 0;
    um_bellek_doldur(g_bengi, 0, sizeof g_bengi);
    if (!yol) return UM_OLDU;
    um_kopya(g_yol, sizeof g_yol, yol);
    if (!up_var_mi(yol)) return UM_OLDU;

    up_tut_t t = up_ac(yol, UP_OKU);
    if (t < 0) return UM_YANLIS;
    static char tampon[6144];
    int64_t n = up_oku(t, tampon, sizeof tampon - 1);
    up_kapat(t);
    if (n <= 0) return UM_OLDU;
    tampon[n] = 0;

    char satir[288]; uint32_t s = 0;
    for (int64_t i = 0; i <= n; i++) {
        char ch = tampon[i];
        if (ch == '\n' || ch == 0) {
            satir[s] = 0;
            /* tur|dogrulandi|anahtar|deger  (eski biçim: anahtar|deger) */
            char *p[4] = { satir, 0, 0, 0 };
            int alan = 0;
            for (uint32_t j = 0; j < s && alan < 3; j++)
                if (satir[j] == '|') { satir[j] = 0; p[++alan] = satir + j + 1; }
            if (alan == 3 && p[3])
                bengi_kaydet(tur_coz(p[0]), p[2], p[3], p[1][0] == '1');
            else if (alan == 1 && p[1] && satir[0])
                bengi_kaydet(BENGI_KULLANICI, p[0], p[1], 1);
            s = 0;
        } else if (s + 1 < sizeof satir) satir[s++] = ch;
    }
    g_kirli = 0;
    return UM_OLDU;
}

int bengi_sakla(void) {
    if (!g_yol[0]) return UM_YOK;
    if (!g_kirli) return UM_OLDU;
    if (!up_yetkim_var_mi(YET_DOSYA_YAZ)) return UM_YETKISIZ;
    up_tut_t t = up_ac(g_yol, UP_YAZ | UP_YARAT);
    if (t < 0) return UM_YANLIS;
    char satir[288];
    for (int i = 0; i < g_sayi; i++) {
        if (!g_bengi[i].dolu) continue;
        uint32_t n = um_bicim(satir, sizeof satir, "%s|%d|%s|%s\n",
                              bengi_tur_adi(g_bengi[i].tur), g_bengi[i].dogrulandi,
                              g_bengi[i].anahtar, g_bengi[i].deger);
        up_yaz(t, satir, n);
    }
    up_kapat(t);
    g_kirli = 0;
    return UM_OLDU;
}
