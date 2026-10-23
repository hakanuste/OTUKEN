/* Ekran birlestirici — bkz. include/ekran.h */
#include "ekran.h"
#include "fb.h"
#include "gui.h"
#include "ulgen.h"
#include "ulgen_theme.h"
#include "heap.h"

static int hx0, hy0, hx1, hy1, hvar;     /* hasar birlesimi [x0,x1) x [y0,y1) */

/* MASAUSTU KATMANI (1.4.8): tema duvar kagidi + simgeler + rihtim.
 * Durağan; bir kez cizilip burada tutulur, hasarli bolgeye kopyalanir.
 * Kurulamazsa (arka tampon yok / bellek yok) duz renk kullanilir. */
static uint32_t *masa;
static int masa_w, masa_h;

int ekran_masaustu_var(void) { return masa != 0; }
uint32_t ekran_masaustu_pikseli(int x, int y) {
    if (!masa || x < 0 || y < 0 || x >= masa_w || y >= masa_h) return EKRAN_MASAUSTU_RENK;
    return masa[(uint64_t)y * masa_w + x];
}

void ekran_masaustu_hazirla(void) {
    int sw = fb_width(), sh = fb_height();
    if (sw <= 0 || sh <= 0 || !fb_arka_tampon_var()) return;
    if (!masa || masa_w != sw || masa_h != sh) {
        if (masa) kfree(masa);
        masa = (uint32_t *)kmalloc((size_t)sw * (size_t)sh * sizeof(uint32_t));
        masa_w = sw; masa_h = sh;
        if (!masa) return;
    }
    fb_kirp_t eski = fb_kirp_al();
    fb_kirp_sifirla();
    ulgen_theme_arka_ciz();                         /* arka tampona */
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) masa[(uint64_t)y * sw + x] = fb_get_pixel(x, y);
    fb_kirp_geri(eski);
    ekran_tam_hasar();                              /* ustune pencereler yeniden */
}

void ekran_hasar(int x, int y, int w, int h) {
    int sw = fb_width(), sh = fb_height();
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > sw ? sw : x + w, y1 = y + h > sh ? sh : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    if (!hvar) { hx0 = x0; hy0 = y0; hx1 = x1; hy1 = y1; hvar = 1; return; }
    if (x0 < hx0) hx0 = x0;
    if (y0 < hy0) hy0 = y0;
    if (x1 > hx1) hx1 = x1;
    if (y1 > hy1) hy1 = y1;
}

void ekran_tam_hasar(void) { ekran_hasar(0, 0, fb_width(), fb_height()); }
int  ekran_hasar_var(void) { return hvar; }

/* Cizilecek oge: tur 1 = GUI penceresi, 2 = ULGEN yuzeyi */
typedef struct { int tur, id, z, x, y, w, h; } oge_t;

void ekran_isle(void) {
    if (!hvar) return;
    int x0 = hx0, y0 = hy0, x1 = hx1, y1 = hy1;
    hvar = 0;

    oge_t o[GUI_PENCERE_SAYISI + ULGEN_MAX_YUZEY];
    int n = 0;
    for (int i = 0; i < GUI_PENCERE_SAYISI; i++) {
        int x, y, w, h, g;
        if (gui_pencere_bilgi(i, &x, &y, &w, &h, &g) != 0 || !g) continue;
        o[n].tur = 1; o[n].id = i; o[n].z = gui_pencere_z(i);
        o[n].x = x; o[n].y = y; o[n].w = w; o[n].h = h; n++;
    }
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
        const ulgen_yuzey_t *s = ulgen_yuzey_al(i);
        if (!s || !s->gorunur) continue;
        o[n].tur = 2; o[n].id = i; o[n].z = s->z;
        ulgen_cerceve(i, &o[n].x, &o[n].y, &o[n].w, &o[n].h); n++;
    }
    for (int i = 1; i < n; i++) {                    /* z'ye gore artan (alttan uste) */
        oge_t t = o[i]; int j = i;
        while (j > 0 && o[j - 1].z > t.z) { o[j] = o[j - 1]; j--; }
        o[j] = t;
    }

    fb_kirp_t eski = fb_kirp_al();
    fb_kirp_sifirla();
    fb_kirp_daralt(x0, y0, x1 - x0, y1 - y0);
    fb_kirp_t bolge = fb_kirp_al();

    if (masa) fb_blit(0, 0, masa_w, masa_h, masa, masa_w);     /* kirpma: yalniz bolge */
    else      fb_fill_rect(x0, y0, x1 - x0, y1 - y0, EKRAN_MASAUSTU_RENK);
    for (int i = 0; i < n; i++) {
        if (o[i].x >= x1 || o[i].y >= y1 || o[i].x + o[i].w <= x0 || o[i].y + o[i].h <= y0) continue;
        fb_kirp_geri(bolge);
        fb_kirp_daralt(o[i].x, o[i].y, o[i].w, o[i].h);   /* oge kendi dikdortgeninden tasamaz */
        if (o[i].tur == 1) gui_pencere_ciz_ic(o[i].id);
        else               ulgen_yuzey_ciz_ic(o[i].id);
    }
    fb_kirp_geri(bolge);
    gui_ust_katman_ciz_ic();                            /* gorev cubugu + imlec: her zaman ustte */
    fb_kirp_geri(eski);
}
