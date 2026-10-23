#include "ulgen.h"
#include "fb.h"
#include "heap.h"
#include "font8x8.h"
#include "log.h"
#include "serial.h"

#include "gorsel_kilit.h"
ozkilit_t gorsel_kilit;   /* bkz. include/gorsel_kilit.h */

#define ULGEN_MASAUSTU_RENK 0x123B4A
#define ULGEN_BASLIK_H      20
#define ULGEN_BASLIK_RENK   0x2E5B4F
#define ULGEN_KENAR_RENK    0x0D1F27
#define ULGEN_BASLIK_YAZI   0xFFFFFF

static ulgen_yuzey_t yuzeyler[ULGEN_MAX_YUZEY];
static int z_sayaci = 1;
static uint32_t birlestirme_sayisi = 0;
static int masaustu_cizildi = 0;

void ulgen_init(void) {
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) yuzeyler[i].used = 0;
    z_sayaci = 1;
    birlestirme_sayisi = 0;
    masaustu_cizildi = 0;
    log_write(LOG_BILGI, "ULGEN", "compositor baslatildi");
}

static int ulgen_yuzey_olustur__ic(const char *baslik, int x, int y, int w, int h) {
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
        if (yuzeyler[i].used) continue;
        ulgen_yuzey_t *s = &yuzeyler[i];

        /* Her yuzeyin KENDI arka tamponu - uygulamalar ekrani hic
         * gormez, sadece buraya cizer. Compositor'un temel fikri budur. */
        s->tampon = (uint32_t *)kmalloc((size_t)w * (size_t)h * 4);
        if (!s->tampon) {
            log_write(LOG_HATA, "ULGEN", "yuzey tamponu ayrilamadi");
            return -1;
        }
        s->used = 1;
        s->gorunur = 1;
        s->x = x; s->y = y; s->w = w; s->h = h;
        s->z = z_sayaci++;
        s->kirli = 1;
        int j = 0;
        while (baslik[j] && j < ULGEN_BASLIK_LEN - 1) { s->baslik[j] = baslik[j]; j++; }
        s->baslik[j] = 0;

        for (int p = 0; p < w * h; p++) s->tampon[p] = 0xE8E4DA;

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
    if (id < 0 || id >= ULGEN_MAX_YUZEY || !yuzeyler[id].used) return -1;
    if (yuzeyler[id].tampon) kfree(yuzeyler[id].tampon);
    yuzeyler[id].used = 0;
    masaustu_cizildi = 0;   /* arkasi acildi - tam yenileme gerek */
    return 0;
}

static int gecerli(int id) {
    return (id >= 0 && id < ULGEN_MAX_YUZEY && yuzeyler[id].used);
}

static void ulgen_doldur__ic(int id, uint32_t renk) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    for (int p = 0; p < s->w * s->h; p++) s->tampon[p] = renk;
    s->kirli = 1;
}

static void ulgen_piksel__ic(int id, int x, int y, uint32_t renk) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    s->tampon[y * s->w + x] = renk;
    s->kirli = 1;
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
    s->kirli = 1;
}

static void ulgen_yazi__ic(int id, int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    if (!gecerli(id)) return;
    ulgen_yuzey_t *s = &yuzeyler[id];
    int cx = x;
    for (int k = 0; str[k]; k++) {
        if (str[k] == '\n') { y += 10; cx = x; continue; }
        const uint8_t *glyph = font8x8[(uint8_t)str[k]];
        for (int row = 0; row < 8; row++) {
            int yy = y + row;
            if (yy < 0 || yy >= s->h) continue;
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                int xx = cx + col;
                if (xx < 0 || xx >= s->w) continue;
                s->tampon[yy * s->w + xx] = (bits & (1 << (7 - col))) ? fg : bg;
            }
        }
        cx += 8;
    }
    s->kirli = 1;
}

static void ulgen_tasi__ic(int id, int x, int y) {
    if (!gecerli(id)) return;
    yuzeyler[id].x = x;
    yuzeyler[id].y = y;
    yuzeyler[id].kirli = 1;
    masaustu_cizildi = 0;   /* eski konum temizlenmeli */
}

static void ulgen_one_getir__ic(int id) {
    if (!gecerli(id)) return;
    yuzeyler[id].z = z_sayaci++;
    yuzeyler[id].kirli = 1;
}

static void ulgen_gorunurluk__ic(int id, int gorunur) {
    if (!gecerli(id)) return;
    yuzeyler[id].gorunur = gorunur;
    yuzeyler[id].kirli = 1;
    if (!gorunur) masaustu_cizildi = 0;
}

/* Bir yuzeyi (cerceve + icerik) ekrana kopyalar. */
static void yuzey_ciz(ulgen_yuzey_t *s) {
    /* baslik cubugu */
    fb_fill_rect(s->x, s->y - ULGEN_BASLIK_H, s->w, ULGEN_BASLIK_H, ULGEN_BASLIK_RENK);
    fb_draw_string(s->x + 5, s->y - ULGEN_BASLIK_H + 6, s->baslik,
                    ULGEN_BASLIK_YAZI, ULGEN_BASLIK_RENK);
    fb_draw_rect(s->x - 1, s->y - ULGEN_BASLIK_H - 1, s->w + 2,
                  s->h + ULGEN_BASLIK_H + 2, ULGEN_KENAR_RENK);

    /* icerik: yuzeyin KENDI tamponundan ekrana */
    for (int j = 0; j < s->h; j++) {
        for (int i = 0; i < s->w; i++) {
            fb_put_pixel(s->x + i, s->y + j, s->tampon[j * s->w + i]);
        }
    }
}

static int ulgen_birlestir__ic(void) {
    int is_var = 0;

    /* SAHIPLIK KURALI: masaustu arka planini eski GUI (gui.c) cizer;
     * ULGEN yalnizca KENDI yuzeylerini onun UZERINE birlestirir.
     * Ikisi de tum ekrani doldurmaya calisirsa birbirini siler
     * (bu gercekten yasandi: ekran tamamen bosaldi). Bu yuzden
     * ULGEN burada arka plani BOYAMAZ. */
    if (!masaustu_cizildi) {
        masaustu_cizildi = 1;
        for (int i = 0; i < ULGEN_MAX_YUZEY; i++)
            if (yuzeyler[i].used) yuzeyler[i].kirli = 1;
    }

    /* z-sirasina gore (kucukten buyuge) birlestir - ustteki en son
     * cizilir ki digerlerinin uzerine binsin.
     * NOT: onceki surumde "pass = hedef_z - 1" seklinde kurnaz bir
     * atlama vardi ve bazi yuzeyleri ATLIYORDU (ikinci yuzey hic
     * cizilmiyordu). Basit ve DOGRU olan: her turda kalanlarin en
     * kucuk z'lisini sec, cizildi olarak isaretle. */
    /* Herhangi bir yuzey kirliyse, TUMUNU z-sirasina gore bir kez
     * yeniden ciz ve bayraklari temizle.
     * NOT: onceki denemede "bir yuzey cizilince ustundekileri kirli
     * isaretle" mantigi eklemistim; bu GERI BESLEME dongusu yaratip
     * surekli yeniden cizime ve titremeye yol acti. Tek seferlik
     * toplu cizim hem daha basit hem DOGRU. */
    int kirli_var = 0;
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++)
        if (yuzeyler[i].used && yuzeyler[i].gorunur && yuzeyler[i].kirli) kirli_var = 1;

    if (kirli_var) {
        int cizildi[ULGEN_MAX_YUZEY];
        for (int i = 0; i < ULGEN_MAX_YUZEY; i++) cizildi[i] = 0;
        for (int tur = 0; tur < ULGEN_MAX_YUZEY; tur++) {
            int sec = -1;
            for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
                if (!yuzeyler[i].used || !yuzeyler[i].gorunur || cizildi[i]) continue;
                if (sec < 0 || yuzeyler[i].z < yuzeyler[sec].z) sec = i;
            }
            if (sec < 0) break;
            cizildi[sec] = 1;
            yuzey_ciz(&yuzeyler[sec]);
            yuzeyler[sec].kirli = 0;
            is_var++;
        }
    }

    if (is_var) birlestirme_sayisi++;
    return is_var;
}

static void ulgen_tam_yenile__ic(void) {
    masaustu_cizildi = 0;
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
