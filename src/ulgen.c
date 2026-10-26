#include "ulgen.h"
#include "fb.h"
#include "heap.h"
#include "log.h"
#include "serial.h"
#include "sched.h"

#include "gorsel_kilit.h"
#include "ekran.h"
#include "gui.h"
ozkilit_t gorsel_kilit;   /* bkz. include/gorsel_kilit.h */

#define ULGEN_MASAUSTU_RENK 0x123B4A
#define ULGEN_BASLIK_H      20
#define ULGEN_BASLIK_RENK   0x2E5B4F
#define ULGEN_KENAR_RENK    0x0D1F27
#define ULGEN_BASLIK_YAZI   0xFFFFFF
#define ULGEN_KAPAT_RENK    0xB33A3A

static ulgen_yuzey_t yuzeyler[ULGEN_MAX_YUZEY];
/* 1.4.8: z-sayaci GUI pencereleriyle ORTAKTIR (bkz. ekran.h). Sifirlanmaz:
 * gui_init ulgen_init'ten ONCE koşar ve kendi pencerelerine buradan z alir. */
static int z_sayaci = 1;
int ulgen_z_ver(void) { return z_sayaci++; }
static uint32_t birlestirme_sayisi = 0;

void ulgen_init(void) {
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) yuzeyler[i].used = 0;
    birlestirme_sayisi = 0;
    log_write(LOG_BILGI, "ULGEN", "compositor baslatildi");
}


/* 1.4.9: gorev cubugu acik pencereleri gosterir: yuzey acilinca, kapaninca,
 * one gelince ya da gizlenince cubuk yeniden cizilmeli. */
static void gorev_cubugu_hasar(void) {
    int sw = fb_width(), sh = fb_height();
    if (sw > 0 && sh > 0) ekran_hasar(0, sh - GUI_GOREV_CUBUGU_H, sw, GUI_GOREV_CUBUGU_H);
}

static int ulgen_yuzey_olustur__ic(const char *baslik, int x, int y, int w, int h) {
    /* 1.4.7: boyut siniri. Eskiden kullanicidan gelen w,h (her biri 65535'e
     * kadar) dogrudan kmalloc(w*h*4)'e gidiyordu: TEK bir pencere cekirdek
     * heap'inin tamamini (64 MB) tuketebiliyordu. Yuzey ekrandan buyuk
     * olamaz; <= 0 boyut reddedilir. */
    /* Cerceve tamponu yoksa (basliksiz acilis) sabit tavan: 1920x1080
     * (8 MB) - yine tek pencere heap'i tuketemez. */
    int azw = fb_width() > 0 ? fb_width() : 1920;
    int azh = fb_height() > 0 ? fb_height() : 1080;
    if (w <= 0 || h <= 0 || w > azw || h > azh) {
        log_write(LOG_UYARI, "ULGEN", "yuzey boyutu sinir disi - reddedildi");
        return -1;
    }
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
        if (yuzeyler[i].used) continue;
        ulgen_yuzey_t *s = &yuzeyler[i];

        /* Her yuzeyin KENDI arka tamponu - uygulamalar ekrani hic
         * gormez, sadece buraya cizer. Compositor'un temel fikri budur. */
        size_t piksel = (size_t)w * (size_t)h;
        s->tampon = (uint32_t *)kmalloc(piksel * 4);
        if (!s->tampon) {
            log_write(LOG_HATA, "ULGEN", "yuzey tamponu ayrilamadi");
            return -1;
        }
        s->used = 1;
        s->gorunur = 1;
        s->sunulan = 0;
        s->elle_sun = 0;
        s->x = x; s->y = y; s->w = w; s->h = h;
        s->z = ulgen_z_ver();
        s->kirli = 1;
        {
            task_t *ben = scheduler_current_task();
            s->sahip = ben ? ben->id : 0;          /* 1.4.7: sahiplik */
        }
        int j = 0;
        while (baslik[j] && j < ULGEN_BASLIK_LEN - 1) { s->baslik[j] = baslik[j]; j++; }
        s->baslik[j] = 0;

        for (size_t p = 0; p < piksel; p++) s->tampon[p] = 0xE8E4DA;   /* size_t: isaretli tasma yok */

        gorev_cubugu_hasar();
        serial_write("ULGEN: yuzey olusturuldu '");
        serial_write(s->baslik);
        serial_write("' kimlik=");
        serial_write_hex((uint32_t)i);
        serial_write("\n");
        return i;
    }
    return -1;
}

static int ulgen_yuzey_yoket__ic(int id) {
    gorev_cubugu_hasar();
    if (id < 0 || id >= ULGEN_MAX_YUZEY || !yuzeyler[id].used) return -1;
    int hx, hy, hw, hh;
    if (ulgen_cerceve(id, &hx, &hy, &hw, &hh) == 0) ekran_hasar(hx, hy, hw, hh);   /* arkasi acildi */
    if (yuzeyler[id].tampon) kfree(yuzeyler[id].tampon);
    if (yuzeyler[id].sunulan) kfree(yuzeyler[id].sunulan);
    yuzeyler[id].sunulan = 0;
    yuzeyler[id].used = 0;
    return 0;
}

static int gecerli(int id) {
    return (id >= 0 && id < ULGEN_MAX_YUZEY && yuzeyler[id].used);
}

static void ulgen_doldur__ic(int id, uint32_t renk) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    for (int p = 0; p < s->w * s->h; p++) s->tampon[p] = renk;
    if (!s->elle_sun) s->kirli = 1;   /* uygulama yuzeyi: ekrana ulgen_sun ile */
}

static void ulgen_piksel__ic(int id, int x, int y, uint32_t renk) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    s->tampon[y * s->w + x] = renk;
    if (!s->elle_sun) s->kirli = 1;   /* uygulama yuzeyi: ekrana ulgen_sun ile */
}

static void ulgen_dikdortgen__ic(int id, int x, int y, int w, int h, uint32_t renk) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= s->h) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= s->w) continue;
            s->tampon[yy * s->w + xx] = renk;
        }
    }
    if (!s->elle_sun) s->kirli = 1;   /* uygulama yuzeyi: ekrana ulgen_sun ile */
}

static void ulgen_yazi__ic(int id, int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    int saydam = (bg == ULGEN_SAYDAM);
    int cx = x;
    const char *p = str;
    while (*p) {
        if (*p == '\n') { y += FB_SATIR_YUK; cx = x; p++; continue; }
        const uint8_t *g = fb_glif(fb_utf8_kod(&p));
        for (int row = 0; row < FB_YAZI_YUK; row++) {
            int yy = y + row;
            if (yy < 0 || yy >= s->h) continue;
            for (int col = 0; col < FB_YAZI_EN; col++) {
                int xx = cx + col;
                if (xx < 0 || xx >= s->w) continue;
                uint32_t a = g[row * FB_YAZI_EN + col];
                uint32_t *hp = &s->tampon[yy * s->w + xx];
                uint32_t zemin = saydam ? *hp : bg;
                if (saydam && a == 0) continue;
                *hp = (a == 255) ? fg : (a == 0) ? zemin : fb_karistir(fg, zemin, a);
            }
        }
        cx += FB_YAZI_EN;
    }
    if (!s->elle_sun) s->kirli = 1;   /* uygulama yuzeyi: ekrana ulgen_sun ile */
}

static void ulgen_tasi__ic(int id, int x, int y) {
    if (!gecerli(id)) return;
    {   /* eski konum: arkasi yeniden cizilmeli (hayalet kalmasin) */
        int hx, hy, hw, hh;
        ulgen_cerceve(id, &hx, &hy, &hw, &hh);
        ekran_hasar(hx, hy, hw, hh);
    }
    yuzeyler[id].x = x;
    yuzeyler[id].y = y;
    yuzeyler[id].kirli = 1;           /* yeni konum birlestirmede hasarlanir */
}

static void ulgen_one_getir__ic(int id) {
    gorev_cubugu_hasar();
    if (!gecerli(id)) return;
    yuzeyler[id].z = ulgen_z_ver();
    yuzeyler[id].kirli = 1;
}

static void ulgen_gorunurluk__ic(int id, int gorunur) {
    gorev_cubugu_hasar();
    if (!gecerli(id)) return;
    yuzeyler[id].gorunur = gorunur;
    yuzeyler[id].kirli = 1;
    {   /* gizlenince arkasi, gorununce kendisi yeniden cizilir */
        int hx, hy, hw, hh;
        ulgen_cerceve(id, &hx, &hy, &hw, &hh);
        ekran_hasar(hx, hy, hw, hh);
    }
}

/* Bir yuzeyi (cerceve + icerik) ekrana cizer. Birlestirici (ekran.c)
 * cagirir; kirpma dikdortgeni o an cizilen bolgeyle kesisik olarak
 * ayarlidir, kopya YALNIZ o kesisim icin yapilir. gorsel_kilit tutulur. */
void ulgen_yuzey_ciz_ic(int id) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    /* baslik cubugu + kapatma dugmesi (ulgen_kapat_dugmesi_mi ile ayni yer) */
    fb_fill_rect(s->x, s->y - ULGEN_BASLIK_H, s->w, ULGEN_BASLIK_H, ULGEN_BASLIK_RENK);
    fb_draw_string(s->x + 5, s->y - ULGEN_BASLIK_H + 2, s->baslik,
                    ULGEN_BASLIK_YAZI, ULGEN_BASLIK_RENK);
    {
        int bx = s->x + s->w - 20, by = s->y - ULGEN_BASLIK_H + 2;
        fb_fill_rect(bx, by, 16, 16, ULGEN_KAPAT_RENK);
        fb_kod_ciz(bx + 4, by, 'x', 0xFFFFFF, ULGEN_KAPAT_RENK);
    }
    fb_draw_rect(s->x - 1, s->y - ULGEN_BASLIK_H - 1, s->w + 2,
                  s->h + ULGEN_BASLIK_H + 2, ULGEN_KENAR_RENK);

    /* icerik: yuzeyin KENDI tamponundan, kirpma ile kesisen kisim */
    fb_kirp_t k = fb_kirp_al();
    int x0 = s->x > k.x0 ? s->x : k.x0, x1 = s->x + s->w < k.x1 ? s->x + s->w : k.x1;
    int y0 = s->y > k.y0 ? s->y : k.y0, y1 = s->y + s->h < k.y1 ? s->y + s->h : k.y1;
    for (int y = y0; y < y1; y++) {
        const uint32_t *kaynak = (s->elle_sun && s->sunulan) ? s->sunulan : s->tampon;
        const uint32_t *satir = &kaynak[(y - s->y) * s->w];
        for (int x = x0; x < x1; x++) fb_put_pixel(x, y, satir[x - s->x]);
    }
}

/* Kirli yuzeylerin cercevesini HASARLI isaretler ve birlestiriciye
 * cizdirir. Birlestirici o bolgeyi masaustu + butun ogeler (GUI
 * pencereleri dahil) z-sirasiyla yeniden cizer: bir yuzey artik
 * altindakini bilmeden "ustune boyanmaz". Donus: kirli yuzey sayisi. */
static int ulgen_birlestir__ic(void) {
    int is_var = 0;
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
        ulgen_yuzey_t *s = &yuzeyler[i];
        if (!s->used || !s->kirli) continue;
        s->kirli = 0;
        if (!s->gorunur) continue;
        int hx, hy, hw, hh;
        ulgen_cerceve(i, &hx, &hy, &hw, &hh);
        ekran_hasar(hx, hy, hw, hh);
        is_var++;
    }
    ekran_isle();          /* tasima/gizleme/yok etme hasari da burada islenir */
    if (is_var) birlestirme_sayisi++;
    return is_var;
}

static void ulgen_tam_yenile__ic(void) {
    ekran_tam_hasar();
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++)
        if (yuzeyler[i].used) yuzeyler[i].kirli = 1;
}

int ulgen_yuzey_sayisi(void) {
    int n = 0;
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) if (yuzeyler[i].used) n++;
    return n;
}

const ulgen_yuzey_t *ulgen_yuzey_al(int id) {
    if (!gecerli(id)) return 0;
    return &yuzeyler[id];
}

uint32_t ulgen_birlestirme_sayisi(void) { return birlestirme_sayisi; }

/* =====================================================================
 * Asama 42: olay kuyrugu. Kuyruk doluysa EN ESKI olay dusurulur;
 * girdi birikmesi uygulamayi bloklamamalidir.
 * ===================================================================== */
static void ulgen_olay_it__ic(int id, uint32_t tur, uint32_t tus, int x, int y) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    int yeni = (s->olay_son + 1) % ULGEN_OLAY_KUYRUK;
    if (yeni == s->olay_bas) s->olay_bas = (s->olay_bas + 1) % ULGEN_OLAY_KUYRUK;
    s->olay[s->olay_son].tur = tur;
    s->olay[s->olay_son].tus = tus;
    s->olay[s->olay_son].x = x;
    s->olay[s->olay_son].y = y;
    s->olay_son = yeni;
}

static int ulgen_olay_cek__ic(int id, ulgen_olay_t *cikti) {
    if (!gecerli(id) || !cikti) return 0;
    ulgen_yuzey_t *s = &yuzeyler[id];
    if (s->olay_bas == s->olay_son) return 0;
    *cikti = s->olay[s->olay_bas];
    s->olay_bas = (s->olay_bas + 1) % ULGEN_OLAY_KUYRUK;
    return 1;
}

static int ulgen_kapat_dugmesi_mi__ic(int id, int x, int y) {
    if (!gecerli(id)) return 0;
    ulgen_yuzey_t *s = &yuzeyler[id];
    int bx = s->x + s->w - 20, by = s->y - ULGEN_BASLIK_H + 2;
    return (x >= bx && x < bx + 16 && y >= by && y < by + 16);
}

static int ulgen_noktadaki_yuzey__ic(int x, int y) {
    int en_iyi = -1, en_z = -1;
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
        ulgen_yuzey_t *s = &yuzeyler[i];
        if (!s->used || !s->gorunur) continue;
        if (x >= s->x && x < s->x + s->w &&
            y >= s->y - ULGEN_BASLIK_H && y < s->y + s->h && s->z > en_z) {
            en_z = s->z; en_iyi = i;
        }
    }
    return en_iyi;
}

/* 1.4.9: klavye odagindaki yuzey = gorunur yuzeylerin EN USTTEKI (tiklama
 * one getirir). Eskiden "pencere" odagindaki tuslari, olay yoklayan HER
 * uygulama yarisarak cekiyordu (UMAY ile ikinci bir uygulama acikken
 * tuslar rastgele bolunuyordu). */
int ulgen_odak_yuzey(void) {
    ozkilit_al(&gorsel_kilit);
    int en_iyi = -1, en_z = -1;
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
        ulgen_yuzey_t *s = &yuzeyler[i];
        if (s->used && s->gorunur && s->z > en_z) { en_z = s->z; en_iyi = i; }
    }
    ozkilit_birak(&gorsel_kilit);
    return en_iyi;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int ulgen_yuzey_olustur(const char *baslik, int x, int y, int w, int h) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_yuzey_olustur__ic(baslik, x, y, w, h);
    ozkilit_birak(&gorsel_kilit);
    return r;
}

int ulgen_yuzey_yoket(int id) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_yuzey_yoket__ic(id);
    ozkilit_birak(&gorsel_kilit);
    return r;
}

void ulgen_doldur(int id, uint32_t renk) {
    ozkilit_al(&gorsel_kilit);
    ulgen_doldur__ic(id, renk);
    ozkilit_birak(&gorsel_kilit);
}

void ulgen_piksel(int id, int x, int y, uint32_t renk) {
    ozkilit_al(&gorsel_kilit);
    ulgen_piksel__ic(id, x, y, renk);
    ozkilit_birak(&gorsel_kilit);
}

void ulgen_dikdortgen(int id, int x, int y, int w, int h, uint32_t renk) {
    ozkilit_al(&gorsel_kilit);
    ulgen_dikdortgen__ic(id, x, y, w, h, renk);
    ozkilit_birak(&gorsel_kilit);
}

void ulgen_yazi(int id, int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    ozkilit_al(&gorsel_kilit);
    ulgen_yazi__ic(id, x, y, str, fg, bg);
    ozkilit_birak(&gorsel_kilit);
}

void ulgen_tasi(int id, int x, int y) {
    ozkilit_al(&gorsel_kilit);
    ulgen_tasi__ic(id, x, y);
    ozkilit_birak(&gorsel_kilit);
}

void ulgen_one_getir(int id) {
    ozkilit_al(&gorsel_kilit);
    ulgen_one_getir__ic(id);
    ozkilit_birak(&gorsel_kilit);
}

void ulgen_gorunurluk(int id, int gorunur) {
    ozkilit_al(&gorsel_kilit);
    ulgen_gorunurluk__ic(id, gorunur);
    ozkilit_birak(&gorsel_kilit);
}

int ulgen_birlestir(void) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_birlestir__ic();
    ozkilit_birak(&gorsel_kilit);
    return r;
}

void ulgen_tam_yenile(void) {
    ozkilit_al(&gorsel_kilit);
    ulgen_tam_yenile__ic();
    ozkilit_birak(&gorsel_kilit);
}

void ulgen_olay_it(int id, uint32_t tur, uint32_t tus, int x, int y) {
    ozkilit_al(&gorsel_kilit);
    ulgen_olay_it__ic(id, tur, tus, x, y);
    ozkilit_birak(&gorsel_kilit);
}

int ulgen_olay_cek(int id, ulgen_olay_t *cikti) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_olay_cek__ic(id, cikti);
    ozkilit_birak(&gorsel_kilit);
    return r;
}

int ulgen_yuzey_sahibi(int id, uint32_t *sahip) {
    ozkilit_al(&gorsel_kilit);
    int var = gecerli(id);
    if (var && sahip) *sahip = yuzeyler[id].sahip;
    ozkilit_birak(&gorsel_kilit);
    return var ? 0 : -1;
}

int ulgen_gorev_bitti(uint32_t gorev_id) {
    int n = 0;
    ozkilit_al(&gorsel_kilit);
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++)
        if (yuzeyler[i].used && yuzeyler[i].sahip == gorev_id && gorev_id != 0) {
            ulgen_yuzey_yoket__ic(i);          /* tamponu da iade eder */
            n++;
        }
    ozkilit_birak(&gorsel_kilit);
    return n;
}

int ulgen_noktadaki_yuzey(int x, int y) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_noktadaki_yuzey__ic(x, y);
    ozkilit_birak(&gorsel_kilit);
    return r;
}

int ulgen_kapat_dugmesi_mi(int id, int x, int y) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_kapat_dugmesi_mi__ic(id, x, y);
    ozkilit_birak(&gorsel_kilit);
    return r;
}

/* Yuzeyin ekrandaki DIS cercevesi (kenar + baslik cubugu + icerik). */
int ulgen_cerceve(int id, int *x, int *y, int *w, int *h) {
    if (!gecerli(id)) return -1;
    const ulgen_yuzey_t *s = &yuzeyler[id];
    *x = s->x - 1; *y = s->y - ULGEN_BASLIK_H - 1;
    *w = s->w + 2; *h = s->h + ULGEN_BASLIK_H + 2;
    return 0;
}

/* --- 1.4.8 sunum --- */
static int ulgen_elle_sun__ic(int id) {
    if (!gecerli(id)) return -1;
#ifdef ULGEN_SUNUM_KAPALI
    return 0;      /* KONTROL GRUBU (yalniz sinama): sunum yok, eski davranis */
#endif
    ulgen_yuzey_t *s = &yuzeyler[id];
    if (!s->sunulan) {
        size_t n = (size_t)s->w * (size_t)s->h;
        s->sunulan = (uint32_t *)kmalloc(n * 4);
        if (!s->sunulan) return -1;
        for (size_t p = 0; p < n; p++) s->sunulan[p] = s->tampon[p];
    }
    s->elle_sun = 1;
    return 0;
}

static int ulgen_sun__ic(int id) {
    if (!gecerli(id)) return -1;
    ulgen_yuzey_t *s = &yuzeyler[id];
    if (s->elle_sun && s->sunulan) {
        size_t n = (size_t)s->w * (size_t)s->h;
        for (size_t p = 0; p < n; p++) s->sunulan[p] = s->tampon[p];
    }
    s->kirli = 1;
    return 0;
}

int ulgen_elle_sun(int id) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_elle_sun__ic(id);
    ozkilit_birak(&gorsel_kilit);
    return r;
}

int ulgen_sun(int id) {
    ozkilit_al(&gorsel_kilit);
    int r = ulgen_sun__ic(id);
    ozkilit_birak(&gorsel_kilit);
    return r;
}
