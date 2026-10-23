/* =====================================================================
 * ÜLGEN / GUI çizim hattı regresyon sınaması (1024x768) — bkz. ekran_sinama.h
 *
 * Sahne gerçek API'lerle kurulur (gui_*, ulgen_*, log_write) ve her
 * adımdan sonra çekirdek ana döngüsünün ekran adımı (masaustu_tur)
 * çağrılır. Denetimler çizim kodunu KULLANMAZ: harmanlama, UTF-8
 * çözümü, glif arama ve z-sırası modeli burada ayrıca yazılmıştır.
 * Ortak olan yalnız yazı tipi verisidir (yazi16.h: gliflerin kendisi).
 *
 * Sabitler gui.c/ulgen.c ile aynıdır (başlık yüksekliği, renkler);
 * değişirlerse bu dosya da güncellenmelidir — sınama bunu da yakalar.
 * ===================================================================== */
#include "ekran_sinama.h"
#include "fb.h"
#include "gui.h"
#include "ulgen.h"
#include "log.h"
#include "mouse.h"
#include "yazi16.h"
#include "ekran.h"
#include "ulgen_theme.h"

#define SW 1024
#define SH 768

/* gui.c renkleri */
#define G_MASAUSTU   0x123B4Au
#define G_GOREVCUB   0x0D1F27u
#define G_WINBG      0xE8E4DAu
#define G_TITLE      0x2E5B4Fu
#define G_TITLE_ODAK 0x3E8B72u
#define G_TITLEFG    0xFFFFFFu
#define G_BORDER     0x0D1F27u
#define G_TEXTFG     0x1A1A1Au
#define G_CLOSE      0xB33A3Au
#define G_TASKBTN    0x1A3038u
#define G_TASKFG     0xE8E4DAu
#define G_LOG_UYARI  0x8A6D1Fu
#define G_TITLEBAR_H 22
#define G_TASKBAR_H  28
/* ulgen.c renkleri */
#define U_BASLIK     0x2E5B4Fu
#define U_KENAR      0x0D1F27u
#define U_BASLIK_YAZ 0xFFFFFFu

static ekran_yaz_t yaz;
static int toplam_hata;

/* ---------------- küçük biçimleyiciler (libc yok) ---------------- */
static char *s_ek(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *s_sayi(char *d, long v) {
    char t[24]; int n = 0;
    if (v < 0) { *d++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = t[--n];
    *d = 0; return d;
}
static char *s_hex(char *d, uint64_t v, int basamak) {
    static const char h[] = "0123456789ABCDEF";
    for (int i = basamak - 1; i >= 0; i--) *d++ = h[(v >> (i * 4)) & 0xF];
    *d = 0; return d;
}

/* ---------------- bağımsız harmanlama / UTF-8 / glif ---------------- */
static uint32_t t_kar(uint32_t f, uint32_t b, uint32_t a) {
    uint32_t r = (((f >> 16) & 255) * a + ((b >> 16) & 255) * (255 - a) + 127) / 255;
    uint32_t g = (((f >> 8) & 255) * a + ((b >> 8) & 255) * (255 - a) + 127) / 255;
    uint32_t c = ((f & 255) * a + (b & 255) * (255 - a) + 127) / 255;
    return (r << 16) | (g << 8) | c;
}

/* Katı UTF-8 çözücü; geçersiz baytta 0xFFFD */
static uint32_t t_utf8(const char **pp) {
    const unsigned char *p = (const unsigned char *)*pp;
    if (p[0] < 0x80) { *pp += 1; return p[0]; }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *pp += 2; return ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *pp += 3; return ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    *pp += 1; return 0xFFFD;
}

static int t_glif_no(uint32_t k) {
    for (int i = 0; i < YAZI_GLIF_SAYISI; i++) if (yazi16_kod[i] == k) return i;
    return -1;
}

static int t_utf8_yaz(uint32_t k, char *d) {
    if (k < 0x80) { d[0] = (char)k; return 1; }
    if (k < 0x800) { d[0] = (char)(0xC0 | (k >> 6)); d[1] = (char)(0x80 | (k & 0x3F)); return 2; }
    d[0] = (char)(0xE0 | (k >> 12)); d[1] = (char)(0x80 | ((k >> 6) & 0x3F)); d[2] = (char)(0x80 | (k & 0x3F));
    return 3;
}

/* ---------------- renk kümeleri (sıralı, ikili arama) ---------------- */
typedef struct { uint32_t v[2600]; int n; } kume_t;

static void k_bosalt(kume_t *k) { k->n = 0; }
static void k_ekle(kume_t *k, uint32_t c) { if (k->n < 2600) k->v[k->n++] = c & 0xFFFFFF; }
static void k_harman(kume_t *k, uint32_t f, uint32_t b) {
    for (uint32_t a = 0; a < 256; a++) k_ekle(k, t_kar(f, b, a));
}
static void k_sirala(kume_t *k) {          /* yerinde kabuk sıralaması + tekilleştirme */
    for (int ara = k->n / 2; ara > 0; ara /= 2)
        for (int i = ara; i < k->n; i++) {
            uint32_t t = k->v[i]; int j = i;
            while (j >= ara && k->v[j - ara] > t) { k->v[j] = k->v[j - ara]; j -= ara; }
            k->v[j] = t;
        }
    int m = 0;
    for (int i = 0; i < k->n; i++) if (m == 0 || k->v[m - 1] != k->v[i]) k->v[m++] = k->v[i];
    k->n = m;
}
static int k_var(const kume_t *k, uint32_t c) {
    int lo = 0, hi = k->n - 1;
    while (lo <= hi) {
        int o = (lo + hi) / 2;
        if (k->v[o] == c) return 1;
        if (k->v[o] < c) lo = o + 1; else hi = o - 1;
    }
    return 0;
}

static kume_t k_gui, k_cerceve, k_gorev;
#define Y_AZAMI 8
static kume_t k_yuzey[Y_AZAMI];

/* ---------------- z-sırası modeli (alttan üste) ---------------- */
#define T_GUI   1
#define T_YUZEY 2
typedef struct { int tur, id; } m_oge_t;
static m_oge_t mz[16];
static int mn;

static void m_ekle(int tur, int id) { mz[mn].tur = tur; mz[mn].id = id; mn++; }
static void m_one(int tur, int id) {
    int i = 0;
    while (i < mn && !(mz[i].tur == tur && mz[i].id == id)) i++;
    if (i == mn) return;
    for (; i < mn - 1; i++) mz[i] = mz[i + 1];
    mz[mn - 1].tur = tur; mz[mn - 1].id = id;
}

typedef struct { int x, y, w, h, gorunur; } dik_t;
static int oge_dik(const m_oge_t *o, dik_t *d) {
    if (o->tur == T_GUI) return gui_pencere_bilgi(o->id, &d->x, &d->y, &d->w, &d->h, &d->gorunur);
    const ulgen_yuzey_t *s = ulgen_yuzey_al(o->id);
    if (!s || ulgen_cerceve(o->id, &d->x, &d->y, &d->w, &d->h) != 0) return -1;
    d->gorunur = s->gorunur;
    return 0;
}

/* Modeldeki geometrinin önbelleği (her denetimde yenilenir) */
static dik_t md[16];
static int imlec_x, imlec_y;

/* (x,y)'nin beklenen sahibi: -2 görev çubuğu, -3 imleç, -1 masaüstü, >=0 model sırası */
/* -4: ust cubuk (her zaman ustte) */
static int sahip(int x, int y) {
    if (x >= imlec_x && x < imlec_x + 8 && y >= imlec_y && y < imlec_y + 8) return -3;
    if (y >= SH - G_TASKBAR_H) return -2;
    if (y < ULGEN_UST_BAR) return -4;
    for (int i = mn - 1; i >= 0; i--) {
        const dik_t *d = &md[i];
        if (d->gorunur && x >= d->x && x < d->x + d->w && y >= d->y && y < d->y + d->h) return i;
    }
    return -1;
}

/* ---------------- sahne kaydı ---------------- */
typedef struct { int yid, x, y; const char *s; uint32_t fg, bg; } yazi_t;
static yazi_t yazilar[32];
static int yazi_n;

static void y_yazi(int yid, int x, int y, const char *s, uint32_t fg, uint32_t bg, uint32_t alt) {
    ulgen_yazi(yid, x, y, s, fg, bg);
    yazilar[yazi_n].yid = yid; yazilar[yazi_n].x = x; yazilar[yazi_n].y = y;
    yazilar[yazi_n].s = s; yazilar[yazi_n].fg = fg;
    yazilar[yazi_n].bg = (bg == ULGEN_SAYDAM) ? alt : bg;   /* saydamda alttaki bilinen renk */
    yazi_n++;
}

/* ---------------- denetimler ---------------- */
static void satir(const char *s) { if (yaz) yaz(s); }

static void md_yenile(void) {
    for (int i = 0; i < mn; i++) if (oge_dik(&mz[i], &md[i]) != 0) md[i].gorunur = 0;
}

/* Ust cubugun sahne basindaki anlik goruntusu (saat sabit) */
static uint32_t ust_bar[SW * 48];

/* 1) piksel sahipliği */
static int sahiplik_denetle(void) {
    md_yenile();
    int hata = 0;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            int o = sahip(x, y);
            if (o == -3) continue;
            uint32_t p = fb_get_pixel(x, y);
            int iyi;
            if (o == -2)      iyi = k_var(&k_gorev, p);
            else if (o == -4) iyi = (p == ust_bar[y * SW + x]);          /* ust cubuk birebir */
            else if (o == -1) iyi = (p == ekran_masaustu_pikseli(x, y)); /* masaustu katmani birebir */
            else if (mz[o].tur == T_GUI) iyi = k_var(&k_gui, p);
            else {
                const ulgen_yuzey_t *s = ulgen_yuzey_al(mz[o].id);
                int icerik = x >= s->x && x < s->x + s->w && y >= s->y && y < s->y + s->h;
                iyi = icerik ? k_var(&k_yuzey[mz[o].id], p) : k_var(&k_cerceve, p);
            }
            if (!iyi) {
                if (hata < 4) {
                    char b[160], *d = b;
                    d = s_ek(d, "    sahiplik: ("); d = s_sayi(d, x); d = s_ek(d, ","); d = s_sayi(d, y);
                    d = s_ek(d, ") renk=0x"); d = s_hex(d, p, 6);
                    d = s_ek(d, " beklenen sahip=");
                    if (o == -2) d = s_ek(d, "gorev cubugu");
                    else if (o == -4) d = s_ek(d, "ust cubuk");
                    else if (o == -1) d = s_ek(d, "masaustu");
                    else { d = s_ek(d, mz[o].tur == T_GUI ? "gui#" : "yuzey#"); d = s_sayi(d, mz[o].id); }
                    s_ek(d, "\n");
                    satir(b);
                }
                hata++;
            }
        }
    return hata;
}

/* 2) yazı: kayıtlı her metin, görünür olduğu her pikselde birebir */
static int yazi_denetle(void) {
    int hata = 0;
    for (int t = 0; t < yazi_n; t++) {
        const yazi_t *yz = &yazilar[t];
        const ulgen_yuzey_t *s = ulgen_yuzey_al(yz->yid);
        if (!s || !s->gorunur) continue;
        int mo = -1;
        for (int i = 0; i < mn; i++) if (mz[i].tur == T_YUZEY && mz[i].id == yz->yid) mo = i;
        const char *p = yz->s;
        int cx = yz->x, cy = yz->y, yerel_hata = 0;
        while (*p) {
            if (*p == '\n') { cy += FB_SATIR_YUK; cx = yz->x; p++; continue; }
            uint32_t k = t_utf8(&p);
            int g = t_glif_no(k);
            if (g < 0) g = t_glif_no('?');
            for (int r = 0; r < YAZI_YUK; r++)
                for (int c = 0; c < YAZI_EN; c++) {
                    int lx = cx + c, ly = cy + r;
                    if (lx < 0 || ly < 0 || lx >= s->w || ly >= s->h) continue;    /* yüzey kırpması */
                    int sx = s->x + lx, sy = s->y + ly;
                    if (sx < 0 || sy < 0 || sx >= SW || sy >= SH) continue;        /* ekran kırpması */
                    if (sahip(sx, sy) != mo) continue;                            /* örtülü */
                    uint32_t a = yazi16[g][r * YAZI_EN + c];
                    uint32_t bek = a == 0 ? yz->bg : a == 255 ? yz->fg : t_kar(yz->fg, yz->bg, a);
                    uint32_t gor = fb_get_pixel(sx, sy);
                    if (gor != bek) {
                        if (yerel_hata < 2) {
                            char b[200], *d = b;
                            d = s_ek(d, "    yazi #"); d = s_sayi(d, t); d = s_ek(d, " '");
                            d = s_ek(d, yz->s); d = s_ek(d, "' ekran("); d = s_sayi(d, sx); d = s_ek(d, ",");
                            d = s_sayi(d, sy); d = s_ek(d, ") gorulen=0x"); d = s_hex(d, gor, 6);
                            d = s_ek(d, " beklenen=0x"); d = s_hex(d, bek, 6); s_ek(d, "\n");
                            satir(b);
                        }
                        yerel_hata++;
                    }
                }
            cx += YAZI_EN;
        }
        hata += yerel_hata;
    }
    return hata;
}

/* 3) ekran belleği = arka tampon (fb_sun sonrası) */
static int vram_denetle(void) {
    if (!fb_arka_tampon_var()) return 0;
    int hata = 0;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            uint32_t a = fb_nicemle(fb_get_pixel(x, y)), v = fb_ekran_pikseli(x, y);
            if (a != v) {
                if (hata < 3) {
                    char b[120], *d = b;
                    d = s_ek(d, "    vram: ("); d = s_sayi(d, x); d = s_ek(d, ","); d = s_sayi(d, y);
                    d = s_ek(d, ") arka=0x"); d = s_hex(d, a, 6); d = s_ek(d, " vram=0x"); d = s_hex(d, v, 6);
                    s_ek(d, "\n"); satir(b);
                }
                hata++;
            }
        }
    return hata;
}

/* Glif hücresini (x,y)'de fg/bg ile arar: glif no, bulunamazsa -1 */
static int hucre_oku(int x, int y, uint32_t fg, uint32_t bg) {
    for (int g = 0; g < YAZI_GLIF_SAYISI; g++) {
        int uy = 1;
        for (int r = 0; r < YAZI_YUK && uy; r++)
            for (int c = 0; c < YAZI_EN; c++) {
                uint32_t a = yazi16[g][r * YAZI_EN + c];
                uint32_t bek = a == 0 ? bg : a == 255 ? fg : t_kar(fg, bg, a);
                if (fb_get_pixel(x + c, y + r) != bek) { uy = 0; break; }
            }
        if (uy) return g;
    }
    return -1;
}

/* GUI penceresinin bir satırını okur: tam görünen hücreler (sahibi o pencere).
 * cikti: UTF-8; bilinmeyen hücre '#'. Dönüş: okunan hücre sayısı */
static int satir_oku(int moge, int x0, int y0, int azami_hucre, char *cikti, int cn) {
    int o = 0, n = 0;
    for (int h = 0; h < azami_hucre; h++) {
        int x = x0 + h * YAZI_EN;
        int tam = 1;
        for (int r = 0; r < YAZI_YUK && tam; r++)
            for (int c = 0; c < YAZI_EN; c++)
                if (x + c >= SW || y0 + r >= SH || sahip(x + c, y0 + r) != moge) { tam = 0; break; }
        if (!tam) break;
        /* pencerenin içerik kırpması: hücre pencere içinde bitmeli */
        int g = hucre_oku(x, y0, G_TEXTFG, G_WINBG);
        char u[4]; int ub;
        if (g < 0) { u[0] = '#'; ub = 1; } else ub = t_utf8_yaz(yazi16_kod[g], u);
        if (o + ub + 1 >= cn) break;
        for (int i = 0; i < ub; i++) cikti[o++] = u[i];
        n++;
    }
    cikti[o] = 0;
    return n;
}

static int m_sira(int tur, int id) {
    for (int i = 0; i < mn; i++) if (mz[i].tur == tur && mz[i].id == id) return i;
    return -1;
}

/* Boşlukları atarak karşılaştırır; a, b'nin ÖNEKİ mi (boşluksuz) */
static int bosluksuz_onek(const char *a, const char *b) {
    for (;;) {
        while (*a == ' ') a++;
        while (*b == ' ') b++;
        if (!*a) return 1;
        if (*a != *b) return 0;
        a++; b++;
    }
}

static int denetle(const char *ad) {
    int sh = sahiplik_denetle();
    int yh = yazi_denetle();
    int vh = vram_denetle();
    int top = sh + yh + vh;
    char b[160], *d = b;
    d = s_ek(d, "EKRAN-SINAMA "); d = s_ek(d, ad); d = s_ek(d, top ? ": KALDI" : ": GECTI");
    d = s_ek(d, " (sahiplik="); d = s_sayi(d, sh); d = s_ek(d, " yazi="); d = s_sayi(d, yh);
    d = s_ek(d, " vram="); d = s_sayi(d, vh); s_ek(d, ")\n");
    satir(b);
    toplam_hata += top ? 1 : 0;
    return top;
}

static void tek(const char *ad, int iyi, const char *ayrinti) {
    char b[400], *d = b;
    d = s_ek(d, "EKRAN-SINAMA "); d = s_ek(d, ad); d = s_ek(d, iyi ? ": GECTI" : ": KALDI");
    if (ayrinti) { d = s_ek(d, " "); d = s_ek(d, ayrinti); }
    s_ek(d, "\n");
    satir(b);
    if (!iyi) toplam_hata++;
}

static uint64_t ozet_arka, ozet_vram;
uint64_t ekran_sinama_ozet(void) { return ozet_arka; }
uint64_t ekran_sinama_vram_ozet(void) { return ozet_vram; }

static void ozet_hesapla(void) {
    uint64_t a = 1469598103934665603ULL, v = a;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            uint32_t p = fb_get_pixel(x, y), q = fb_ekran_pikseli(x, y);
            for (int i = 0; i < 3; i++) {
                a ^= (p >> (i * 8)) & 255; a *= 1099511628211ULL;
                v ^= (q >> (i * 8)) & 255; v *= 1099511628211ULL;
            }
        }
    ozet_arka = a; ozet_vram = v;
}

/* ---------------- sahne metinleri ---------------- */
static const char TERM_METIN[] =
    "Yer altı kileri yıl boyu 12-15 derecede kalır; Horasan harcıyla örülür. "
    "Çığ düşmüş dağ yolunda şoför ağır yükü güçlükle çekiyor: ÇĞİÖŞÜ çğıöşü.";

static const char U_UZUN[] =
    "Uzun satır: öğrenci ağaçları sulayıp çiçekleri düzenlerken güneş batıyordu — kırpılmalı";

int ekran_sinamasi(ekran_yaz_t yazici) {
    yaz = yazici;
    toplam_hata = 0;
    mn = 0; yazi_n = 0;
    if (fb_width() != SW || fb_height() != SH) {
        satir("EKRAN-SINAMA: ATLANDI (ekran 1024x768 degil)\n");
        return 0;
    }
    int mb;
    mouse_get_state(&imlec_x, &imlec_y, &mb);
    (void)mb;

    /* --- renk kümeleri --- */
    k_bosalt(&k_gui);
    k_harman(&k_gui, G_TEXTFG, G_WINBG);
    k_harman(&k_gui, G_CLOSE, G_WINBG);
    k_harman(&k_gui, G_LOG_UYARI, G_WINBG);
    k_harman(&k_gui, G_TITLEFG, G_TITLE);
    k_harman(&k_gui, G_TITLEFG, G_TITLE_ODAK);
    k_harman(&k_gui, G_TITLEFG, G_CLOSE);
    k_ekle(&k_gui, G_BORDER);
    k_sirala(&k_gui);
    k_bosalt(&k_cerceve);
    k_harman(&k_cerceve, U_BASLIK_YAZ, U_BASLIK);
    k_harman(&k_cerceve, U_BASLIK_YAZ, G_CLOSE);      /* kapatma dugmesi */
    k_ekle(&k_cerceve, U_KENAR);
    k_sirala(&k_cerceve);
    k_bosalt(&k_gorev);
    k_harman(&k_gorev, G_TASKFG, G_GOREVCUB);
    k_harman(&k_gorev, G_TASKFG, G_TASKBTN);
    k_harman(&k_gorev, G_TASKFG, G_TITLE);
    k_sirala(&k_gorev);

    /* --- GUI pencereleri: farklı boyutlar, kenarlar --- */
    ulgen_theme_saat_sabitle(1790759340);      /* belirlenimci ust cubuk: 2026-09-30 12:09 (TSI) */
    gui_terminal_temizle();
    masaustu_tur(0);
    for (int y = 0; y < ULGEN_UST_BAR && y < 48; y++)
        for (int x = 0; x < SW; x++) ust_bar[y * SW + x] = fb_get_pixel(x, y);
    gui_pencere_yerlestir(GUI_TERMINAL, 40, 40, 424, 300, 1);
    gui_pencere_yerlestir(GUI_GUNLUK, 0, 470, 420, 250, 1);      /* x=0; dar: satırlar kırpılmalı */
    gui_pencere_yerlestir(GUI_DOSYA, 860, 120, 300, 200, 1);     /* sağ kenardan taşar */
    gui_pencere_yerlestir(GUI_GORUNTU, 260, 170, 460, 260, 0);
    /* gui_init z-sırası: dosya, görüntüleyici, terminal, günlük */
    m_ekle(T_GUI, GUI_DOSYA); m_ekle(T_GUI, GUI_GORUNTU);
    m_ekle(T_GUI, GUI_TERMINAL); m_ekle(T_GUI, GUI_GUNLUK);

    for (int i = 0; i < 16; i++)
        log_write(LOG_BILGI, "EKRAN",
                  "Çekirdek günlüğü uzun satırı: ğüşıöç ĞÜŞİÖÇ harfleriyle pencere kırpma sınaması");

    /* --- ULGEN yüzeyleri --- */
    int O = ulgen_yuzey_olustur("OTAĞ — ajan meclisi", 0, 0, 220, 300);   /* x=0,y=0: başlık ekran dışı */
    int U = ulgen_yuzey_olustur("UMAY — Ötüken", 300, 90, 560, 340);
    int K = ulgen_yuzey_olustur("köşe", 924, 708, 200, 120);            /* sağ-alt kenar, görev çubuğu */
    if (O < 0 || U < 0 || K < 0 || O >= Y_AZAMI || U >= Y_AZAMI || K >= Y_AZAMI) {
        satir("EKRAN-SINAMA: KALDI (yuzey olusturulamadi)\n");
        return 1;
    }
    m_ekle(T_YUZEY, O); m_ekle(T_YUZEY, U); m_ekle(T_YUZEY, K);

    k_bosalt(&k_yuzey[O]); k_harman(&k_yuzey[O], 0xF0D090, 0x20303A); k_sirala(&k_yuzey[O]);
    k_bosalt(&k_yuzey[U]); k_harman(&k_yuzey[U], 0xE0B24A, 0x0E1512);
    k_harman(&k_yuzey[U], 0x7FC8A9, 0x16211C); k_sirala(&k_yuzey[U]);
    k_bosalt(&k_yuzey[K]); k_harman(&k_yuzey[K], 0xFFFFFF, 0x402040); k_sirala(&k_yuzey[K]);

    ulgen_doldur(O, 0x20303A);
    y_yazi(O, 0, 0, "köşe0", 0xF0D090, 0x20303A, 0);                     /* x=0,y=0 */
    y_yazi(O, 8, 20, "OTAĞ — ajan meclisi", 0xF0D090, 0x20303A, 0);
    y_yazi(O, 8, 40, "BİLGE  boş", 0xF0D090, 0x20303A, 0);
    y_yazi(O, 8, 56, "ĞÜŞİÖÇ ğüşıöç", 0xF0D090, 0x20303A, 0);
    y_yazi(O, 180, 76, "sınırda metin", 0xF0D090, 0x20303A, 0);           /* sağ kırpma */
    y_yazi(O, 8, 292, "alt kenar yazısı", 0xF0D090, 0x20303A, 0);         /* alt kırpma */

    ulgen_doldur(U, 0x0E1512);
    y_yazi(U, 12, 8, "UMAY — Dede Korkut dili ile", 0xE0B24A, 0x0E1512, 0);
    y_yazi(U, 12, 30, U_UZUN, 0xE0B24A, 0x0E1512, 0);                     /* kırpma kenarı */
    y_yazi(U, 548, 50, "ğşı", 0xE0B24A, 0x0E1512, 0);                     /* çok baytlı harf kenarda */
    y_yazi(U, 12, 70, "iki satır:\nikinci satır ÇĞŞ", 0xE0B24A, 0x0E1512, 0);
    y_yazi(U, 12, 330, "son satır (alt kırpma)", 0xE0B24A, 0x0E1512, 0);
    ulgen_dikdortgen(U, 12, 110, 300, 20, 0x16211C);
    y_yazi(U, 16, 112, "saydam yazı çğış", 0x7FC8A9, ULGEN_SAYDAM, 0x16211C);

    ulgen_doldur(K, 0x402040);
    y_yazi(K, 0, 0, "x=924", 0xFFFFFF, 0x402040, 0);
    y_yazi(K, 76, 10, "sağ-alt köşe ğşı", 0xFFFFFF, 0x402040, 0);        /* x=1023'te kesilir */
    y_yazi(K, 4, 110, "alt", 0xFFFFFF, 0x402040, 0);                      /* y=767'de kesilir */

    masaustu_tur(0);
    denetle("A-ilk-sahne");

    /* B: terminale uzun Türkçe metin (kısa yol çizimi) — z değişmemeli */
    gui_write_string(TERM_METIN);
    masaustu_tur(0);
    denetle("B-terminal-yazdi");

    /* C: terminal başlığına tık -> terminal EN ÜSTE (ULGEN yüzeylerinin de) */
    {
        int tx, ty, tw, th, tg;
        gui_pencere_bilgi(GUI_TERMINAL, &tx, &ty, &tw, &th, &tg);
        gui_fare_isle(260, ty + 8, 1);
        gui_fare_isle(260, ty + 8, 0);
        m_one(T_GUI, GUI_TERMINAL);
        masaustu_tur(0);
        denetle("C-terminal-one");

        /* terminal içeriğini glif glif oku: harf kaybı/bölünme olmamalı */
        char metin[1200]; int mo = 0; int bilinmeyen = 0;
        /* icerik alani: baslik alti ile alt kenar cizgisi (th-1) arasi;
         * yalniz bu alana TAM sigan satirlar okunur */
        int satirlar = (th - 1 - (G_TITLEBAR_H + 6)) / FB_SATIR_YUK;
        int ms = m_sira(T_GUI, GUI_TERMINAL);
        for (int r = 0; r < satirlar && mo < 1000; r++) {
            char s[256];
            satir_oku(ms, tx + 8, ty + G_TITLEBAR_H + 6 + r * FB_SATIR_YUK, (tw - 16) / YAZI_EN, s, sizeof s);
            for (int i = 0; s[i] && mo < 1100; i++) { if (s[i] == '#') bilinmeyen++; metin[mo++] = s[i]; }
        }
        metin[mo] = 0;
        char b[300], *d = b;
        d = s_ek(d, "(bilinmeyen hucre="); d = s_sayi(d, bilinmeyen); s_ek(d, ")");
        tek("C2-terminal-metni-birebir", bilinmeyen == 0 && bosluksuz_onek(TERM_METIN, metin), b);
        if (!(bilinmeyen == 0 && bosluksuz_onek(TERM_METIN, metin))) {
            satir("    okunan: "); satir(metin); satir("\n");
        }
    }

    /* D: UMAY öne -> terminalin üstünde */
    ulgen_one_getir(U);
    m_one(T_YUZEY, U);
    masaustu_tur(0);
    denetle("D-umay-one");

    /* E: UMAY içeriğine tık (altında terminal var): terminal öne GELMEMELİ,
     *    UMAY yüzey-yerel koordinatla TIK olayı almalı */
    {
        ulgen_olay_t e;
        while (ulgen_olay_cek(U, &e)) {}
        gui_fare_isle(350, 200, 1);
        gui_fare_isle(350, 200, 0);
        masaustu_tur(0);
        denetle("E-umay-tik");
        int var = ulgen_olay_cek(U, &e);
        char b[120], *d = b;
        d = s_ek(d, "(olay="); d = s_sayi(d, var ? (long)e.tur : -1);
        d = s_ek(d, " x="); d = s_sayi(d, var ? e.x : -1); d = s_ek(d, " y="); d = s_sayi(d, var ? e.y : -1);
        s_ek(d, " beklenen TIK 50,110)");
        tek("E2-tik-olayi", var && e.tur == ULGEN_OLAY_TIK && e.x == 50 && e.y == 110, b);
    }

    /* F: OTAĞ taşınır: eski yerinde hayalet kalmamalı */
    ulgen_tasi(O, 480, 440);
    masaustu_tur(0);
    denetle("F-otag-tasindi");

    /* G: periyodik tam yenileme: aynı z-sırası */
    masaustu_tur(1);
    denetle("G-periyodik");

    /* H: köşe yüzeyi gizlenir */
    ulgen_gorunurluk(K, 0);
    masaustu_tur(0);
    denetle("H-kose-gizli");

    /* K: UYGULAMA yüzeyi (SYS_PENCERE_AC gibi "elle sun"): uygulama
     *    karesini parça parça çizerken birleştirici YARIM kareyi göstermemeli;
     *    ekranda son SUNULAN kare kalmalı (UMAY'ın yan sütunu eksik kareler). */
    {
        int P = ulgen_yuzey_olustur("uygulama", 620, 560, 300, 140);
        if (P < 0 || P >= Y_AZAMI || ulgen_elle_sun(P) != 0) {
            tek("K-uygulama-yuzeyi", 0, "(olusturulamadi)");
        } else {
            m_ekle(T_YUZEY, P);
            k_bosalt(&k_yuzey[P]); k_harman(&k_yuzey[P], 0xC0FFC0, 0x103010); k_sirala(&k_yuzey[P]);
            ulgen_doldur(P, 0x103010);
            y_yazi(P, 8, 8, "sunulan kare 1: ağaç", 0xC0FFC0, 0x103010, 0);
            int kare1 = yazi_n - 1;
            ulgen_sun(P);
            masaustu_tur(0);
            denetle("K1-sunulan-kare");

            /* ikinci kare yarıda: zemin başka renge boyandı, SUNULMADI */
            ulgen_doldur(P, 0x301010);
            ulgen_yazi(P, 8, 30, "yarım", 0xFFFFFF, 0x301010);
            masaustu_tur(0);
            masaustu_tur(1);                      /* periyodik tam yenilemede de */
            denetle("K2-yarim-kare-gorunmez");

            /* ikinci kare tamamlanıp sunulur */
            yazilar[kare1].yid = -1;              /* 1. karenin yazısı artık yok */
            k_bosalt(&k_yuzey[P]); k_harman(&k_yuzey[P], 0xFFE0A0, 0x301010);
            k_harman(&k_yuzey[P], 0xFFFFFF, 0x301010); k_sirala(&k_yuzey[P]);
            y_yazi(P, 8, 50, "sunulan kare 2: çiçek", 0xFFE0A0, 0x301010, 0);
            ulgen_sun(P);
            masaustu_tur(0);
            denetle("K3-ikinci-kare");
        }
    }

    /* Günlük penceresi: en yeni satır glif glif; UTF-8 bölünmemeli, pencere kenarında kesilmeli */
    {
        int gx, gy, gw, gh, gg;
        gui_pencere_bilgi(GUI_GUNLUK, &gx, &gy, &gw, &gh, &gg);
        char s[400];
        int n = satir_oku(m_sira(T_GUI, GUI_GUNLUK), gx + 8, gy + G_TITLEBAR_H + 6, 200, s, sizeof s);
        const char *bek = "[BILGI] EKRAN: Çekirdek günlüğü uzun satırı: ğüşıöç ĞÜŞİÖÇ harfleriyle pencere kırpma sınaması";
        /* beklenen: ilk n harf */
        char on[400]; int o = 0; const char *p = bek;
        for (int i = 0; i < n && *p; i++) { uint32_t k = t_utf8(&p); o += t_utf8_yaz(k, on + o); }
        on[o] = 0;
        int ayni = 1;
        for (int i = 0; on[i] || s[i]; i++) if (on[i] != s[i]) { ayni = 0; break; }
        int sigan = (gw - 16) / YAZI_EN;        /* tam sığan hücre */
        char b[160], *d = b;
        d = s_ek(d, "(okunan hucre="); d = s_sayi(d, n); d = s_ek(d, " sigan="); d = s_sayi(d, sigan); s_ek(d, ")");
        tek("I-gunluk-satiri", ayni && n >= sigan - 1 && n <= sigan + 1, b);
        if (!ayni) { satir("    okunan: "); satir(s); satir("\n    beklenen: "); satir(on); satir("\n"); }
    }

    /* L: masaustu tiklamalari. Gunluk ve iki yuzey gizlenir, rihtim gorunur.
     *    Rihtimdan ORKUN -> terminal one, BITIG -> dosya yoneticisi, UMAY ->
     *    UMAY yuzeyi; masaustu simgesi (OTUKENOS) -> dosya yoneticisi. */
    {
        gui_pencere_yerlestir(GUI_GUNLUK, 0, 470, 420, 250, 0);
        ulgen_gorunurluk(O, 0);
        for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
            const ulgen_yuzey_t *y = ulgen_yuzey_al(i);
            if (y && i != U && i != O && i != K && y->gorunur) ulgen_gorunurluk(i, 0);   /* uygulama yuzeyi */
        }
        masaustu_tur(0);
        denetle("L1-rihtim-gorunur");

        static const struct { int k; int tur; int id; const char *ad; } T[] = {
            { 2, T_GUI, GUI_TERMINAL, "L2-rihtim-orkun-terminal" },
            { 1, T_GUI, GUI_DOSYA,    "L3-rihtim-bitig-dosya" },
            { 0, T_YUZEY, -1,         "L4-rihtim-umay" },
        };
        for (int t = 0; t < 3; t++) {
            int x, y, b;
            ulgen_theme_rihtim_konum(T[t].k, &x, &y, &b);
            int cx = x + b / 2, cy = y + b / 2;
            md_yenile();
            if (sahip(cx, cy) != -1) { tek(T[t].ad, 0, "(rihtim simgesi ortulu: sahne hatasi)"); continue; }
            gui_fare_isle(cx, cy, 1);
            gui_fare_isle(cx, cy, 0);
            m_one(T[t].tur, T[t].tur == T_YUZEY ? U : T[t].id);
            masaustu_tur(0);
            denetle(T[t].ad);
        }
        {   /* masaustu simgesi: OTUKENOS (sol sutun, ilk simge) */
            int cx = 30, cy = ULGEN_UST_BAR + 26 + 30;
            md_yenile();
            if (sahip(cx, cy) != -1) tek("L5-simge-dosya", 0, "(simge ortulu: sahne hatasi)");
            else {
                gui_fare_isle(cx, cy, 1);
                gui_fare_isle(cx, cy, 0);
                m_one(T_GUI, GUI_DOSYA);
                masaustu_tur(0);
                denetle("L5-simge-dosya");
            }
        }
    }

    ozet_hesapla();
    {
        char b[120], *d = b;
        d = s_ek(d, "EKRAN-SINAMA ozet arka=0x"); d = s_hex(d, ozet_arka, 16);
        d = s_ek(d, " vram=0x"); d = s_hex(d, ozet_vram, 16); s_ek(d, "\n");
        satir(b);
    }
    {
        char b[80], *d = b;
        d = s_ek(d, "EKRAN-SINAMA SONUC: "); d = s_ek(d, toplam_hata ? "KALDI" : "GECTI");
        d = s_ek(d, " (kalan denetim="); d = s_sayi(d, toplam_hata); s_ek(d, ")\n");
        satir(b);
    }
    return toplam_hata;
}
