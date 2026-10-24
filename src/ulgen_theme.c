#include "ulgen_theme.h"
#include "tema.h"
#include "tamga.h"
#include "fb.h"
#include "duvar.h"
#include "simge.h"
#include "tamga_gorsel.h"
#include "mouse.h"
#include "orbitron.h"
#include "timer.h"
#include "rtc.h"

#include "gui.h"
#include "kervan2.h"

#define TAMGA_N 15
#define RIHTIM_KARO 42

/* Rıhtım kimliği -> tamga kimliği (sıra tamga_id_t ile aynıdır). */
/* Rol yazıları hücre genişliğine (yaklaşık 75 piksel) göre kısaltıldı;
 * tam karşılıkları üzerine gelince günlükte gösterilir. */
/* Roller BÜYÜK HARF: Orbitron bir başlık yazı tipidir; 10 pikselde
 * küçük harfleri okunmuyordu ("Yapay Us" ekranda "Yaoau Us" çıkıyordu). */
static const char *ROLLER[TAMGA_N] = {
    "SİSTEM", "YAPAY US", "BİLGELİK", "GRAFİK", "TARAYICI",
    "GÜVENLİK", "YETKİ", "KURTARMA", "KURULUM",
    "DOSYA", "AĞ", "PAKET", "POLİTİKA",
    "KİMLİK", "ARAÇLAR"
};

static int __attribute__((unused)) uzunluk(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* --- yardımcı çizimler (tamsayı; FPU kapalı) --- */
static void __attribute__((unused)) cizgi(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 - x0, sx = dx >= 0 ? 1 : -1; if (dx < 0) dx = -dx;
    int dy = y1 - y0, sy = dy >= 0 ? 1 : -1; if (dy < 0) dy = -dy;
    int e = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        fb_put_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = e;
        if (e2 > -dx) { e -= dy; x0 += sx; }
        if (e2 <  dy) { e += dx; y0 += sy; }
    }
}

static void __attribute__((unused)) dag(int taban, int zirve, int x0, int x1, uint32_t c) {
    int xm = (x0 + x1) / 2;
    cizgi(x0, taban, xm, zirve, c);
    cizgi(xm, zirve, x1, taban, c);
}

/* Dolu daire: ay için. Yarıçap karesi karşılaştırmasıyla. */
static void __attribute__((unused)) daire_dolu(int cx, int cy, int r, uint32_t c) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) fb_put_pixel(cx + x, cy + y, c);
}

/* =====================================================================
 * 1.4.8: TEMA ARTIK BAĞLI. Birleştirici (src/ekran.c) iki katman ister:
 *   ulgen_theme_arka_ciz()   DURAĞAN masaüstü: duvar kâğıdı, simgeler,
 *                            rıhtım. Açılışta ve tema değişince bir kez
 *                            çizilir, bellekte tutulur; pencereler bunun
 *                            ÜSTÜNDE birleştirilir.
 *   ulgen_theme_ust_bar_ciz() üst çubuk: HER ZAMAN üstte (görev çubuğu
 *                            gibi); çalışma süresi saniyede bir yenilenir.
 * Çizim ve tıklama AYNI yerleşim işlevlerini kullanır (rihtim_geo,
 * simge_konum): eskiden tıklama 15 tamganın hepsine göre ve farklı
 * payla hesaplanıyor, yanlış tamga dönüyordu.
 * Küçük harfli yazılar DejaVu (fb yazısı) ile çizilir: Orbitron'un küçük
 * harfleri bozuk ("Belgeler" -> "Beloeler"); Orbitron büyük harf başlıklarda.
 * ===================================================================== */

static int g_izgara_acik = 0;
void ulgen_theme_init(void) { }
void ulgen_theme_tazele(void) { }
void ulgen_theme_izgara_degis(void) { g_izgara_acik = !g_izgara_acik; }
int  ulgen_theme_izgara_acik(void)  { return g_izgara_acik; }

/* RIHTIM: günlük kullanımda en sık gerekenler (tamga kimlikleri) */
static const int RIHTIM[] = {
    1,   /* UMAY   — yapay us          */
    9,   /* BİTİG  — dosya sistemi     */
    14,  /* ORKUN  — geliştirici       */
    3,   /* ÜLGEN  — grafik ortamı     */
    4,   /* TULPAR — web tarayıcı      */
    11,  /* KERVAN — paket yönetimi    */
    13   /* TAMGA  — dijital kimlik    */
};
#define RIHTIM_ADET ((int)(sizeof(RIHTIM) / sizeof(RIHTIM[0])))
#define RIHTIM_ARA   10
#define RIHTIM_IC    16
#define RIHTIM_UST   12

typedef struct { int simge, cubuk_en, rx0, cubuk_boy, dy; } rihtim_geo_t;

static void rihtim_geo(int sw, int sh, rihtim_geo_t *g) {
    int azami = sw - 24;
    g->simge = (azami - 2 * RIHTIM_IC - (RIHTIM_ADET - 1) * RIHTIM_ARA) / RIHTIM_ADET;
    if (g->simge > 180) g->simge = 180;
    if (g->simge < 48)  g->simge = 48;
    g->cubuk_en  = RIHTIM_ADET * g->simge + (RIHTIM_ADET - 1) * RIHTIM_ARA + 2 * RIHTIM_IC;
    g->rx0       = (sw - g->cubuk_en) / 2;
    g->cubuk_boy = g->simge + 2 * RIHTIM_UST;
    g->dy        = sh - GUI_GOREV_CUBUGU_H - 8 - g->cubuk_boy;   /* görev çubuğunun hemen üstü */
}

/* k. rıhtım simgesinin sol-üst köşesi */
static void rihtim_simge(const rihtim_geo_t *g, int k, int *x, int *y) {
    *x = g->rx0 + RIHTIM_IC + k * (g->simge + RIHTIM_ARA);
    *y = g->dy + RIHTIM_UST;
}

int ulgen_theme_rihtim_konum(int k, int *x, int *y, int *boy) {
    if (k < 0 || k >= RIHTIM_ADET) return -1;
    rihtim_geo_t g;
    rihtim_geo(fb_width(), fb_height(), &g);
    rihtim_simge(&g, k, x, y);
    *boy = g.simge;
    return RIHTIM[k];
}

/* Masaüstü simgeleri (sol sütun) */
static const struct { simge_id_t id; const char *ad; } MASA[] = {
    { SIMGE_DISK,   "ÖTÜKENOS" },
    { SIMGE_KLASOR, "Belgeler" },
    { SIMGE_KLASOR, "Projeler" },
    { SIMGE_KLASOR, "Arşiv"    },
    { SIMGE_COP,    "Çöp Kutusu" }
};
#define MASA_ADET ((int)(sizeof(MASA) / sizeof(MASA[0])))
#define MASA_X    28
#define MASA_ARA  96
static int masa_y(int i) { return ULGEN_UST_BAR + 26 + i * MASA_ARA; }

/* Gölgeli, ortalanmış DejaVu yazısı */
static void golgeli_ortali(int orta_x, int y, const char *s, uint32_t renk) {
    int x = orta_x - fb_yazi_en(s) / 2;
    fb_yazi_saydam(x + 1, y + 1, s, 0x000000u);
    fb_yazi_saydam(x, y, s, renk);
}

void ulgen_theme_arka_ciz(void) {
    const tema_t *tm = tema_simdiki();
    int sw = fb_width(), sh = fb_height();
    duvar_ciz(0, 0, sw, sh);

    for (int i = 0; i < MASA_ADET; i++) {
        int y = masa_y(i);
        if (y + 84 > sh - GUI_GOREV_CUBUGU_H) break;
        simge_ciz(MASA_X, y, MASA[i].id, 64);
        golgeli_ortali(MASA_X + 32, y + 66, MASA[i].ad, tm->beyaz_yerine_krem);
    }

    /* rıhtım: yarı saydam koyu şerit + tamgalar (üzerine gelme parlaması
     * yok: rıhtım pencerelerin ALTINDA, durağan katmandadır) */
    rihtim_geo_t g;
    rihtim_geo(sw, sh, &g);
    int rx1 = g.rx0 + g.cubuk_en;
    for (int j = 0; j < g.cubuk_boy; j++) {
        static uint32_t satir[2048];
        int en = rx1 - g.rx0;
        if (en > 2048) en = 2048;
        fb_satir_oku(g.rx0, g.dy + j, satir, en);
        int kat = (j < 2) ? 60 : 16;
        for (int i = 0; i < en; i++) {
            uint32_t z = satir[i];
            int zr = (int)((z >> 16) & 0xFF), zg = (int)((z >> 8) & 0xFF), zb = (int)(z & 0xFF);
            satir[i] = ((uint32_t)(zr * 30 / 100 + kat) << 16)
                     | ((uint32_t)(zg * 30 / 100 + kat + 4) << 8)
                     |  (uint32_t)(zb * 30 / 100 + kat + 10);
        }
        int r = 18;
        int dyk = (j < r) ? (r - j) : (j >= g.cubuk_boy - r ? j - (g.cubuk_boy - r - 1) : 0);
        int bas_i = 0, son_i = en;
        if (dyk) {
            int k = 0;
            while (k < r && (r - k) * (r - k) + dyk * dyk > r * r) k++;
            bas_i = k; son_i = en - k;
        }
        if (son_i > bas_i) fb_satir_yaz(g.rx0 + bas_i, g.dy + j, &satir[bas_i], son_i - bas_i);
    }
    for (int k = 0; k < RIHTIM_ADET; k++) {
        int x, y;
        rihtim_simge(&g, k, &x, &y);
        tamga_gorsel_ciz(x, y, RIHTIM[k], g.simge, 0);
    }
}

/* Çalışma süresi: sınamada sabitlenebilir (belirlenimci ekran) */
static int g_saat_sabit = -1;
void ulgen_theme_saat_sabitle(int saniye) { g_saat_sabit = saniye; }

/* Üst çubuk menüsü: çizim ve tıklama AYNI dizeden (tek geometri). */
#define UST_MENU_X 60
static const char UST_MENU[] = "Dosya   Düzen   Görünüm   Uygulamalar   Sistem   Yardım";

/* (x,y) üst çubukta: ÖTÜKEN tamgası -> ULGEN_MENU_TAMGA, menü sözcüğü ->
 * sırası (0 Dosya .. 5 Yardım), boşluk -> -1. */
int ulgen_theme_menu_hit(int x, int y) {
    if (y < 0 || y >= ULGEN_UST_BAR) return -1;
    if (x >= 14 && x < 14 + 34) return ULGEN_MENU_TAMGA;
    char on[sizeof(UST_MENU)];
    int sira = 0, i = 0;
    while (UST_MENU[i]) {
        while (UST_MENU[i] == ' ') i++;
        int bas = i;
        while (UST_MENU[i] && UST_MENU[i] != ' ') i++;
        for (int k = 0; k < bas; k++) on[k] = UST_MENU[k];
        on[bas] = 0;
        int x0 = UST_MENU_X + fb_yazi_en(on);
        for (int k = 0; k < i; k++) on[k] = UST_MENU[k];
        on[i] = 0;
        int x1 = UST_MENU_X + fb_yazi_en(on);
        if (x >= x0 - 4 && x < x1 + 4) return sira;
        sira++;
    }
    return -1;
}

void ulgen_theme_ust_bar_ciz(void) {
    const tema_t *tm = tema_simdiki();
    int sw = fb_width();
    fb_fill_rect(0, 0, sw, ULGEN_UST_BAR, tm->gorev_cubugu);
    tamga_gorsel_ciz(14, 3, 0, 34, 0);                      /* ÖTÜKEN tamgası */
    fb_yazi_saydam(UST_MENU_X, 12, UST_MENU, tm->gorev_yazi);
    /* 1.4.9: gerçek saat (RTC, yerel dilim). RTC yoksa çalışma süresi. */
    char s[48];
    int i = 0;
    int64_t e = g_saat_sabit >= 0 ? (int64_t)g_saat_sabit : zaman_epoch();
    if (e >= 0) {
        static const char *const GUN[7] = {"Pzt", "Sal", "Çar", "Per", "Cum", "Cmt", "Paz"};
        static const char *const AY[12] = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
                                           "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};
        sys_zaman_t z; int hg;
        zaman_coz(e, OTUKEN_ZAMAN_DILIMI_DK, &z, &hg);
        for (const char *p = GUN[hg]; *p; p++) s[i++] = *p;
        s[i++] = ' ';
        if (z.gun >= 10) s[i++] = (char)('0' + z.gun / 10);
        s[i++] = (char)('0' + z.gun % 10);
        s[i++] = ' ';
        for (const char *p = AY[(z.ay - 1) % 12]; *p; p++) s[i++] = *p;
        s[i++] = ' '; s[i++] = ' ';
        s[i++] = (char)('0' + z.saat / 10); s[i++] = (char)('0' + z.saat % 10);
        s[i++] = ':';
        s[i++] = (char)('0' + z.dakika / 10); s[i++] = (char)('0' + z.dakika % 10);
        s[i] = 0;
    } else {
        uint32_t sn = (uint32_t)(timer_get_ticks() / (timer_hz() ? timer_hz() : 100));
        const char *on = "Açık ";
        while (*on) s[i++] = *on++;
        uint32_t sa = sn / 3600, dk = (sn / 60) % 60, ss = sn % 60;
        if (sa) { if (sa >= 10) s[i++] = (char)('0' + (sa / 10) % 10); s[i++] = (char)('0' + sa % 10); s[i++] = ':'; }
        s[i++] = (char)('0' + dk / 10); s[i++] = (char)('0' + dk % 10);
        s[i++] = ':';
        s[i++] = (char)('0' + ss / 10); s[i++] = (char)('0' + ss % 10);
        s[i] = 0;
    }
    fb_yazi_saydam(sw - fb_yazi_en(s) - 16, 12, s, tm->gorev_yazi);
}

/* Eski tek parça çizim (tema komutu önizlemesi vb. için): arka + üst çubuk */
void ulgen_theme_draw(void) {
    ulgen_theme_arka_ciz();
    ulgen_theme_ust_bar_ciz();
}

/* Tıklanan rıhtım simgesinin TAMGA kimliği; yoksa -1 */
int ulgen_theme_dock_hit(int x, int y) {
    rihtim_geo_t g;
    rihtim_geo(fb_width(), fb_height(), &g);
    for (int k = 0; k < RIHTIM_ADET; k++) {
        int gx, gy;
        rihtim_simge(&g, k, &gx, &gy);
        if (x >= gx && x < gx + g.simge && y >= gy && y < gy + g.simge) return RIHTIM[k];
    }
    return -1;
}

/* Tıklanan masaüstü simgesinin sırası (0..); yoksa -1 */
int ulgen_theme_simge_hit(int x, int y) {
    for (int i = 0; i < MASA_ADET; i++) {
        int sy = masa_y(i);
        if (x >= MASA_X && x < MASA_X + 64 && y >= sy && y < sy + 84) return i;
    }
    return -1;
}

int ulgen_theme_izgara_hit(int x, int y) {
    if (!g_izgara_acik) return -1;
    int sw = fb_width(), sh = fb_height();
    int gen = 5 * 150 + 60, yuk = 3 * 170 + 70;
    int gx = (sw - gen) / 2, gy = (sh - yuk) / 2;
    for (int i = 0; i < TAMGA_N; i++) {
        int r = i / 5, c2 = i % 5;
        int ix = gx + 30 + c2 * 150, iy = gy + 46 + r * 170;
        if (x >= ix && x < ix + 128 && y >= iy && y < iy + 128) return i;
    }
    return -2;
}

/* =====================================================================
 * 1.4.9: "Uygulamalar" açılır menüsü. 15 tamganın hepsi sırayla listelenir;
 * uygulaması olmayanlar (ASENA, ERGENEKON, TAMGA) sönük çizilir ve
 * tıklanınca hiçbir şey açmaz. Çizim ve tıklama AYNI yerleşimi kullanır.
 * ===================================================================== */
#define UM_SATIR 26
#define UM_EN    300
static int g_uyg_menu = 0;
static int g_uyg_sayfa = 0;               /* kurulu paket listesinde sayfa */
/* tamga -> uygulaması var mı (gui.c rihtim_ac ile aynı küme) */
static const uint8_t UM_VAR[TAMGA_N] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };   /* 1.4.9: hepsi */

int  ulgen_theme_uyg_menu_acik(void) { return g_uyg_menu; }
void ulgen_theme_uyg_menu_ata(int acik) { g_uyg_menu = acik ? 1 : 0; if (acik) g_uyg_sayfa = 0; }

/* Paket satırları ekrana sığmazsa sayfalanır: sayfa başına 'adim' öğe ve
 * en altta "sonraki sayfa" satırı. Hiçbir kurulu uygulama ulaşılamaz kalmaz. */
typedef struct { int pn, adim, sayfa_n, bas, gor, sayfali; } paket_duzen_t;
static void paket_duzen(paket_duzen_t *d) {
    d->pn = kervan2_uygulama_sayisi();
    int satir = (fb_height() - ULGEN_UST_BAR - GUI_GOREV_CUBUGU_H - 8) / UM_SATIR - TAMGA_N - 1;
    if (satir < 2) satir = 2;
    d->sayfali = d->pn > satir;
    d->adim = d->sayfali ? satir - 1 : (d->pn ? d->pn : 1);
    d->sayfa_n = (d->pn + d->adim - 1) / d->adim;
    if (d->sayfa_n < 1) d->sayfa_n = 1;
    if (g_uyg_sayfa >= d->sayfa_n) g_uyg_sayfa = 0;
    d->bas = g_uyg_sayfa * d->adim;
    d->gor = d->pn - d->bas < d->adim ? d->pn - d->bas : d->adim;
    if (d->gor < 0) d->gor = 0;
}
void ulgen_theme_uyg_menu_sonraki(void) {
    paket_duzen_t d; paket_duzen(&d);
    g_uyg_sayfa = (g_uyg_sayfa + 1) % d.sayfa_n;
}
int  ulgen_theme_uyg_var(int id) { return id >= 0 && id < TAMGA_N && UM_VAR[id]; }

void ulgen_theme_uyg_menu_kutu(int *x, int *y, int *w, int *h) {
    /* "Uygulamalar" sözcüğünün altı (menü sırası 3) */
    char on[sizeof(UST_MENU)];
    int sira = 0, i = 0, x0 = UST_MENU_X;
    while (UST_MENU[i]) {
        while (UST_MENU[i] == ' ') i++;
        if (sira == 3) {
            for (int k = 0; k < i; k++) on[k] = UST_MENU[k];
            on[i] = 0;
            x0 = UST_MENU_X + fb_yazi_en(on);
            break;
        }
        while (UST_MENU[i] && UST_MENU[i] != ' ') i++;
        sira++;
    }
    paket_duzen_t d; paket_duzen(&d);                 /* 1.4.9: kurulu paket uygulamaları */
    int ps = d.pn ? 1 + (d.sayfali ? d.adim + 1 : d.pn) : 0;   /* ayraç + öğeler (+ sayfa satırı) */
    *x = x0 - 8; *y = ULGEN_UST_BAR; *w = UM_EN; *h = (TAMGA_N + ps) * UM_SATIR + 8;
}

int ulgen_theme_uyg_menu_hit(int x, int y) {
    if (!g_uyg_menu) return -1;
    int mx, my, mw, mh;
    ulgen_theme_uyg_menu_kutu(&mx, &my, &mw, &mh);
    if (x < mx || x >= mx + mw || y < my || y >= my + mh) return -1;
    int r = (y - my - 4) / UM_SATIR;
    if (y - my < 4 || r < 0) return -2;
    if (r < TAMGA_N) return r;
    int pi = r - TAMGA_N - 1;                         /* ayraç satırı: -2 */
    paket_duzen_t d; paket_duzen(&d);
    if (pi >= 0 && pi < d.gor) return ULGEN_MENU_PAKET + d.bas + pi;
    if (d.sayfali && pi == d.adim) return ULGEN_MENU_PAKET_SAYFA;
    return -2;
}

void ulgen_theme_uyg_menu_ciz(void) {
    if (!g_uyg_menu) return;
    const tema_t *tm = tema_simdiki();
    int mx, my, mw, mh;
    ulgen_theme_uyg_menu_kutu(&mx, &my, &mw, &mh);
    fb_fill_rect(mx, my, mw, mh, tm->gorev_cubugu);
    fb_fill_rect(mx, my + mh - 1, mw, 1, tm->kenar);
    fb_fill_rect(mx, my, 1, mh, tm->kenar);
    fb_fill_rect(mx + mw - 1, my, 1, mh, tm->kenar);
    uint32_t sonuk = ((tm->gorev_yazi >> 1) & 0x7F7F7Fu) + ((tm->gorev_cubugu >> 1) & 0x7F7F7Fu);
    for (int i = 0; i < TAMGA_N; i++) {
        int y = my + 4 + i * UM_SATIR;
        const tamga_t *t = tamga_al((tamga_id_t)i);
        uint32_t c = UM_VAR[i] ? tm->gorev_yazi : sonuk;
        fb_yazi_saydam(mx + 14, y + 5, t ? t->ad : "?", c);
        fb_yazi_saydam(mx + 150, y + 5, ROLLER[i], UM_VAR[i] ? tm->vurgu : sonuk);
    }
    paket_duzen_t d; paket_duzen(&d);
    if (d.pn) {
        int y = my + 4 + TAMGA_N * UM_SATIR;
        fb_fill_rect(mx + 10, y + 12, mw - 20, 1, sonuk);
        fb_yazi_saydam(mx + 14, y + 5, "KURULU PAKETLER", sonuk);
        for (int i = 0; i < d.gor; i++) {
            char dosya[13], paket[24];
            if (kervan2_uygulama_al(d.bas + i, dosya, paket) != 0) continue;
            int yy = y + (i + 1) * UM_SATIR;
            fb_yazi_saydam(mx + 14, yy + 5, dosya, tm->gorev_yazi);
            fb_yazi_saydam(mx + 150, yy + 5, paket, sonuk);
        }
        if (d.sayfali) {
            char e[40]; int k = 0;
            const char *on = "sonraki sayfa >> ";
            while (*on) e[k++] = *on++;
            int a = g_uyg_sayfa + 1, b = d.sayfa_n;
            if (a >= 10) e[k++] = (char)('0' + a / 10);
            e[k++] = (char)('0' + a % 10); e[k++] = '/';
            if (b >= 10) e[k++] = (char)('0' + b / 10);
            e[k++] = (char)('0' + b % 10); e[k] = 0;
            fb_yazi_saydam(mx + 14, y + (d.adim + 1) * UM_SATIR + 5, e, tm->vurgu);
        }
    }
}

const char *ulgen_theme_name(int id) {
    const tamga_t *t = tamga_al((tamga_id_t)id);
    return t ? t->ad : "";
}

const char *ulgen_theme_role(int id) {
    return (id >= 0 && id < TAMGA_N) ? ROLLER[id] : "";
}
