/* ORTAK — ayar, günlük, kip. V9.2 ortak.c'nin cJSON'suz ÖTÜKEN karşılığı. */
#include "umay_ortak.h"
#include "umay_port.h"
#include "umay_str.h"
#include <stdarg.h>

typedef struct { char anahtar[32]; char deger[96]; int dolu; } ayar_t;
static ayar_t  g_ayar[AYAR_MAX];
static char    g_ayar_yol[UMAY_YOL_MAX];
static char    g_gun_yol[UMAY_YOL_MAX];
static kip_t   g_kip = KIP_DINGIN;

static const struct { const char *a; const char *d; } VARSAYILAN[] = {
    { "kullanici",     "Hakan" },
#ifdef UMAY_OTUKEN
    /* ÖTÜKEN'in FAT16 birimi DÜZ köktür: alt dizin yok, adlar 8.3.
     * "/kut/umay" orada hiç açılamıyordu, kütüphane hep boş kalıyordu. */
    { "kutuphane",     "/" },
#else
    { "kutuphane",     "/kut/umay" },
#endif
    { "bengi",         "/kut/umay/bengi.txt" },
    #ifdef UMAY_OTUKEN
    /* ÖTÜKEN'de model FAT16 kökündedir: US1 biçimi, 8.3 ad. */
    { "model",         "/UMAY.US1" },
#else
    { "model",         "/kut/umay/model.gguf" },
#endif
    { "baglam_adet",   "3" },
    { "dil",           "oz-turkce" },
    { "ses",           "kapali" },
    { "onay_omur_ms",  "600000" },
    { 0, 0 }
};

static int ayar_bul(const char *a) {
    for (int i = 0; i < AYAR_MAX; i++)
        if (g_ayar[i].dolu && um_esit(g_ayar[i].anahtar, a)) return i;
    return -1;
}

int ayar_yaz(const char *anahtar, const char *deger) {
    int i = ayar_bul(anahtar);
    if (i < 0) {
        for (i = 0; i < AYAR_MAX && g_ayar[i].dolu; i++) {}
        if (i >= AYAR_MAX) return UM_DOLU;
        um_kopya(g_ayar[i].anahtar, sizeof g_ayar[i].anahtar, anahtar);
        g_ayar[i].dolu = 1;
    }
    um_kopya(g_ayar[i].deger, sizeof g_ayar[i].deger, deger);
    return UM_OLDU;
}

const char *ayar_metin(const char *anahtar, const char *varsayilan) {
    int i = ayar_bul(anahtar);
    return (i < 0) ? varsayilan : g_ayar[i].deger;
}

int64_t ayar_sayi(const char *anahtar, int64_t varsayilan) {
    const char *s = ayar_metin(anahtar, 0);
    if (!s || !*s) return varsayilan;
    int64_t d = 0; int eksi = 0;
    if (*s == '-') { eksi = 1; s++; }
    for (; *s >= '0' && *s <= '9'; s++) d = d * 10 + (*s - '0');
    return eksi ? -d : d;
}

/* "anahtar=deger" satırları — JSON yerine düz metin (ayrıştırıcı 20 satır). */
void ayar_kur(const char *yol) {
    um_bellek_doldur(g_ayar, 0, sizeof g_ayar);
    for (int i = 0; VARSAYILAN[i].a; i++) ayar_yaz(VARSAYILAN[i].a, VARSAYILAN[i].d);
    if (!yol) return;
    um_kopya(g_ayar_yol, sizeof g_ayar_yol, yol);

    up_tut_t t = up_ac(yol, UP_OKU);
    if (t < 0) return;
    static char tampon[2048];
    int64_t n = up_oku(t, tampon, sizeof tampon - 1);
    up_kapat(t);
    if (n <= 0) return;
    tampon[n] = 0;

    char satir[128]; uint32_t s = 0;
    for (int64_t i = 0; i <= n; i++) {
        char c = tampon[i];
        if (c == '\n' || c == 0) {
            satir[s] = 0;
            char *e = 0;
            for (uint32_t j = 0; j < s; j++) if (satir[j] == '=') { satir[j] = 0; e = satir + j + 1; break; }
            if (e && satir[0] && satir[0] != '#') ayar_yaz(satir, e);
            s = 0;
        } else if (s + 1 < sizeof satir) satir[s++] = c;
    }
}

int ayar_sakla(void) {
    if (!g_ayar_yol[0]) return UM_YOK;
    if (!up_yetkim_var_mi(YET_DOSYA_YAZ)) return UM_YETKISIZ;
    up_tut_t t = up_ac(g_ayar_yol, UP_YAZ | UP_YARAT);
    if (t < 0) return UM_YANLIS;
    char satir[160];
    for (int i = 0; i < AYAR_MAX; i++) {
        if (!g_ayar[i].dolu) continue;
        uint32_t n = um_bicim(satir, sizeof satir, "%s=%s\n", g_ayar[i].anahtar, g_ayar[i].deger);
        up_yaz(t, satir, n);
    }
    up_kapat(t);
    return UM_OLDU;
}

void gun_yolu_ayarla(const char *yol) { um_kopya(g_gun_yol, sizeof g_gun_yol, yol); }

void gun_yaz(const char *bicim, ...) {
    char govde[512], satir[600];
    va_list ap; va_start(ap, bicim);
    /* um_bicim va_list almadığından basit yol: doğrudan aktarım */
    uint32_t o = 0;
    for (const char *p = bicim; *p && o + 1 < sizeof govde; p++) {
        if (*p != '%') { govde[o++] = *p; continue; }
        p++;
        if (*p == 's') { const char *s = va_arg(ap, const char*); if (!s) s = "(yok)";
                         while (*s && o + 1 < sizeof govde) govde[o++] = *s++; }
        else if (*p == 'd') { char t[24]; um_sayi_yaz(t, sizeof t, (int64_t)va_arg(ap, int));
                              for (char *q = t; *q && o + 1 < sizeof govde; q++) govde[o++] = *q; }
        else if (*p == 'l') { char t[24]; um_sayi_yaz(t, sizeof t, va_arg(ap, int64_t));
                              for (char *q = t; *q && o + 1 < sizeof govde; q++) govde[o++] = *q; }
        else if (*p == 'u') { char t[24]; um_sayi_yaz(t, sizeof t, (int64_t)va_arg(ap, unsigned));
                              for (char *q = t; *q && o + 1 < sizeof govde; q++) govde[o++] = *q; }
        else if (*p == 'c') { govde[o++] = (char)va_arg(ap, int); }
        else if (*p == '%') { govde[o++] = '%'; }
        else if (!*p) break;
    }
    govde[o] = 0;
    va_end(ap);

    char zaman[24];
    um_sayi_yaz(zaman, sizeof zaman, (int64_t)up_ms());
    um_bicim(satir, sizeof satir, "[%s] %s", zaman, govde);
    up_gunluk(satir);

    if (g_gun_yol[0] && up_yetkim_var_mi(YET_DOSYA_YAZ)) {
        up_tut_t t = up_ac(g_gun_yol, UP_YAZ | UP_EKLE | UP_YARAT);
        if (t >= 0) { up_yaz(t, satir, um_uzun(satir)); up_yaz(t, "\n", 1); up_kapat(t); }
    }
}

kip_t umay_kip(void) { return g_kip; }
void  umay_kip_ayarla(kip_t k) { g_kip = k; }
