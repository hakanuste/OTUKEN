#include "fb.h"
#include "yazi16.h"
#include "heap.h"

/* =====================================================================
 * Cerceve tamponu (framebuffer) — 1.4.8
 *
 * CIFT TAMPON: arka tampon kurulduktan sonra (fb_arka_tampon_kur, heap
 * hazir olunca) butun cizim bellekteki ARKA tampona yapilir; ekrana
 * (VRAM) yalniz fb_sun() ile, degisen dikdortgen (kirli bolge) kadar
 * aktarilir. Eskiden ekran yarim saniyede bir DOGRUDAN VRAM uzerinde
 * silinip yeniden ciziliyordu: titreme ve yarim cizilmis kareler.
 * Arka tampon yoksa (erken acilis, bellek yetmedi) dogrudan VRAM'e
 * yazilir — eski davranis.
 *
 * YAZI: 8x16, kenari yumusatilmis (DejaVu Sans Mono, gen_yazi16.py).
 * Dizeler UTF-8 cozulur; tek baytlik ISO-8859-9 (eski cekirdek
 * dizeleri: \xD6, \xDD ...) Turkce harflere eslenir.
 * ===================================================================== */

static uint8_t  *fb_addr;
static uint32_t  fb_pitch;
static uint32_t  fb_w, fb_h;
static uint8_t   fb_bpp;
static uint8_t   red_pos, red_size, green_pos, green_size, blue_pos, blue_size;

static uint32_t *arka;                 /* 0x00RRGGBB, fb_w * fb_h */
/* KIRPMA DIKDORTGENI [x0,x1) x [y0,y1): butun cizim ilkelleri buna uyar.
 * Birlestirici her pencereyi KENDI dikdortgenine kirparak cizer; boylece
 * hicbir pencerenin cizimi disina tasamaz ve hasarli bolge disina yazilmaz. */
static int cx0, cy0, cx1, cy1;
static int kx0, ky0, kx1, ky1;         /* kirli bolge [x0,x1) x [y0,y1); bos: x0 >= x1 */

static inline void kirlet(int x0, int y0, int x1, int y1) {
    if (x0 < kx0) kx0 = x0;
    if (y0 < ky0) ky0 = y0;
    if (x1 > kx1) kx1 = x1;
    if (y1 > ky1) ky1 = y1;
}
static inline void kirli_sifirla(void) { kx0 = (int)fb_w; ky0 = (int)fb_h; kx1 = 0; ky1 = 0; }

void fb_init(multiboot_info_t *mbi) {
    fb_addr  = (uint8_t *)(uintptr_t)mbi->framebuffer_addr;
    fb_pitch = mbi->framebuffer_pitch;
    fb_w     = mbi->framebuffer_width;
    fb_h     = mbi->framebuffer_height;
    fb_bpp   = mbi->framebuffer_bpp;

    /* framebuffer_type == 1 (direct RGB) icin renk alani konumlari */
    red_pos    = mbi->color_info[0];
    red_size   = mbi->color_info[1];
    green_pos  = mbi->color_info[2];
    green_size = mbi->color_info[3];
    blue_pos   = mbi->color_info[4];
    blue_size  = mbi->color_info[5];

    /* Guvenlik agi: renk bilgisi yoksa ya da anlamsizsa (boylar 0,
     * konumlar cakisiyor) 32/24 bpp icin standart XRGB duzeni kullanilir. */
    if (red_size == 0 || green_size == 0 || blue_size == 0 ||
        red_pos == green_pos || red_pos == blue_pos || green_pos == blue_pos) {
        red_pos = 16; green_pos = 8; blue_pos = 0;
        red_size = green_size = blue_size = 8;
    }
    arka = 0;
    kirli_sifirla();
    fb_kirp_sifirla();
}

void fb_kirp_sifirla(void) { cx0 = 0; cy0 = 0; cx1 = (int)fb_w; cy1 = (int)fb_h; }
fb_kirp_t fb_kirp_al(void) { fb_kirp_t k = { cx0, cy0, cx1, cy1 }; return k; }
void fb_kirp_geri(fb_kirp_t k) { cx0 = k.x0; cy0 = k.y0; cx1 = k.x1; cy1 = k.y1; }
/* Kirpmayi (x,y,w,h) ile KESISTIRIR (yalniz daralir). */
void fb_kirp_daralt(int x, int y, int w, int h) {
    if (x > cx0) cx0 = x;
    if (y > cy0) cy0 = y;
    if (x + w < cx1) cx1 = x + w;
    if (y + h < cy1) cy1 = y + h;
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}

int fb_width(void)  { return (int)fb_w; }
int fb_height(void) { return (int)fb_h; }

/* 0x00RRGGBB -> cihazin bit duzeni (16 bpp'de kanal boyuna indirgenir) */
static inline uint32_t paketle(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    if (red_size < 8)   r >>= (8 - red_size);
    if (green_size < 8) g >>= (8 - green_size);
    if (blue_size < 8)  b >>= (8 - blue_size);
    return (r << red_pos) | (g << green_pos) | (b << blue_pos);
}

static inline void vram_yaz(int x, int y, uint32_t rgb) {
    uint32_t packed = paketle(rgb);
    uint8_t *pixel = fb_addr + (uint64_t)y * fb_pitch + (uint64_t)x * (fb_bpp / 8);
    if (fb_bpp == 32) {
        *(volatile uint32_t *)pixel = packed;
    } else if (fb_bpp == 24) {
        pixel[0] = (uint8_t)(packed);
        pixel[1] = (uint8_t)(packed >> 8);
        pixel[2] = (uint8_t)(packed >> 16);
    } else if (fb_bpp == 16) {
        *(volatile uint16_t *)pixel = (uint16_t)packed;
    }
}

static uint32_t vram_oku(int x, int y) {
    uint8_t *pixel = fb_addr + (uint64_t)y * fb_pitch + (uint64_t)x * (fb_bpp / 8);
    uint32_t packed = 0;
    if (fb_bpp == 32)      packed = *(volatile uint32_t *)pixel;
    else if (fb_bpp == 24) packed = pixel[0] | (pixel[1] << 8) | ((uint32_t)pixel[2] << 16);
    else if (fb_bpp == 16) packed = *(volatile uint16_t *)pixel;
    uint32_t r = (packed >> red_pos)   & ((1u << red_size)   - 1);
    uint32_t g = (packed >> green_pos) & ((1u << green_size) - 1);
    uint32_t b = (packed >> blue_pos)  & ((1u << blue_size)  - 1);
    if (red_size < 8)   r = (r * 255) / ((1u << red_size) - 1);
    if (green_size < 8) g = (g * 255) / ((1u << green_size) - 1);
    if (blue_size < 8)  b = (b * 255) / ((1u << blue_size) - 1);
    return (r << 16) | (g << 8) | b;
}

/* Heap hazir olunca bir kez cagrilir. Basarisizsa dogrudan cizim surer. */
int fb_arka_tampon_kur(void) {
    if (arka || !fb_addr || fb_w == 0 || fb_h == 0) return arka ? 0 : -1;
    uint32_t *t = (uint32_t *)kmalloc((size_t)fb_w * fb_h * sizeof(uint32_t));
    if (!t) return -1;
    for (uint32_t y = 0; y < fb_h; y++)          /* ekranda o an ne varsa */
        for (uint32_t x = 0; x < fb_w; x++)
            t[y * fb_w + x] = vram_oku((int)x, (int)y);
    arka = t;
    kirli_sifirla();
    return 0;
}

int fb_arka_tampon_var(void) { return arka != 0; }

/* Sinama: rgb VRAM'e yazilip geri okunsa ne olur (16 bpp'de kanal kaybi). */
uint32_t fb_nicemle(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    if (red_size < 8)   { r >>= (8 - red_size);   r = (r * 255) / ((1u << red_size) - 1); }
    if (green_size < 8) { g >>= (8 - green_size); g = (g * 255) / ((1u << green_size) - 1); }
    if (blue_size < 8)  { b >>= (8 - blue_size);  b = (b * 255) / ((1u << blue_size) - 1); }
    return (r << 16) | (g << 8) | b;
}

/* Sinama: EKRANDAKI (VRAM) pikseli 0x00RRGGBB olarak okur (arka tampon degil). */
uint32_t fb_ekran_pikseli(int x, int y) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_w || (uint32_t)y >= fb_h) return 0;
    return vram_oku(x, y);
}

/* Kirli bolgeyi VRAM'e aktarir. Cagiran gorsel_kilit'i tutmalidir
 * (cizenlerle yarismasin); panik yolu kilitsiz cagirabilir. */
void fb_sun(void) {
    if (!arka || kx0 >= kx1 || ky0 >= ky1) return;
    int x0 = kx0, y0 = ky0, x1 = kx1, y1 = ky1;
    kirli_sifirla();
    int hizli = (fb_bpp == 32 && red_pos == 16 && green_pos == 8 && blue_pos == 0 &&
                 red_size == 8 && green_size == 8 && blue_size == 8);
    for (int y = y0; y < y1; y++) {
        const uint32_t *k = &arka[(uint64_t)y * fb_w + x0];
        if (hizli) {
            volatile uint32_t *h = (volatile uint32_t *)(fb_addr + (uint64_t)y * fb_pitch) + x0;
            for (int x = x0; x < x1; x++) *h++ = *k++;
        } else {
            for (int x = x0; x < x1; x++) vram_yaz(x, y, *k++);
        }
    }
}

/* Bellekteki bir goruntuyu (0x00RRGGBB, satir adimi 'adim' piksel)
 * (x,y)'ye kopyalar; kirpmaya uyar. Masaustu katmani icin. */
void fb_blit(int x, int y, int w, int h, const uint32_t *kaynak, int adim) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w < cx1 ? x + w : cx1, y1 = y + h < cy1 ? y + h : cy1;
    if (x0 >= x1 || y0 >= y1) return;
    for (int j = y0; j < y1; j++) {
        const uint32_t *k = &kaynak[(uint64_t)(j - y) * adim + (x0 - x)];
        if (arka) {
            uint32_t *h2 = &arka[(uint64_t)j * fb_w + x0];
            for (int i = x0; i < x1; i++) *h2++ = *k++ & 0xFFFFFF;
        } else {
            for (int i = x0; i < x1; i++) vram_yaz(i, j, *k++);
        }
    }
    if (arka) kirlet(x0, y0, x1, y1);
}

/* rgb: 0x00RRGGBB */
void fb_put_pixel(int x, int y, uint32_t rgb) {
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1)
        return;
    if (arka) {
        arka[(uint64_t)y * fb_w + x] = rgb & 0xFFFFFF;
        kirlet(x, y, x + 1, y + 1);
    } else {
        vram_yaz(x, y, rgb);
    }
}

uint32_t fb_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_w || (uint32_t)y >= fb_h)
        return 0;
    return arka ? arka[(uint64_t)y * fb_w + x] : vram_oku(x, y);
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return;
    if (arka) {
        uint32_t c = rgb & 0xFFFFFF;
        for (int j = y0; j < y1; j++) {
            uint32_t *s = &arka[(uint64_t)j * fb_w];
            for (int i = x0; i < x1; i++) s[i] = c;
        }
        kirlet(x0, y0, x1, y1);
    } else {
        for (int j = y0; j < y1; j++)
            for (int i = x0; i < x1; i++) vram_yaz(i, j, rgb);
    }
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb) {
    fb_fill_rect(x, y, w, 1, rgb);
    fb_fill_rect(x, y + h - 1, w, 1, rgb);
    fb_fill_rect(x, y, 1, h, rgb);
    fb_fill_rect(x + w - 1, y, 1, h, rgb);
}

/* ---------------------------- YAZI ---------------------------- */

/* Iki rengi a (0..255) oraninda karistirir: a=255 -> on, 0 -> arka. */
uint32_t fb_karistir(uint32_t on, uint32_t zemin, uint32_t a) {
    uint32_t ters = 255 - a;
    uint32_t r = (((on >> 16) & 0xFF) * a + ((zemin >> 16) & 0xFF) * ters + 127) / 255;
    uint32_t g = (((on >> 8)  & 0xFF) * a + ((zemin >> 8)  & 0xFF) * ters + 127) / 255;
    uint32_t b = (( on        & 0xFF) * a + ( zemin        & 0xFF) * ters + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

/* Kod noktasinin glifi; yoksa '?' */
/* 1.4.9: glif sayisi 173 -> 518 (Kiril, Latin ekleri...). U+0500 altindakiler
 * icin dizin tablosu: ilk kullanimda kurulur (dogrusal arama her harfte 400+
 * karsilastirma demekti). 0 = yok, n = glif n-1. Yaris zararsiz: her yazan
 * ayni degerleri yazar. */
#define GLIF_DIZIN_SINIR 0x0500
static uint16_t glif_dizin[GLIF_DIZIN_SINIR];
static volatile int glif_dizin_hazir = 0;

const uint8_t *fb_glif(uint32_t kod) {
    if (kod >= 32 && kod < 127) return yazi16[kod - 32];      /* ASCII sirali */
    if (kod < GLIF_DIZIN_SINIR) {
        if (!__atomic_load_n(&glif_dizin_hazir, __ATOMIC_ACQUIRE)) {
            for (int i = 95; i < YAZI_GLIF_SAYISI; i++)
                if (yazi16_kod[i] < GLIF_DIZIN_SINIR) glif_dizin[yazi16_kod[i]] = (uint16_t)(i + 1);
            __atomic_store_n(&glif_dizin_hazir, 1, __ATOMIC_RELEASE);
        }
        uint16_t d = glif_dizin[kod];
        return d ? yazi16[d - 1] : yazi16['?' - 32];
    }
    for (int i = 95; i < YAZI_GLIF_SAYISI; i++)
        if (yazi16_kod[i] == kod) return yazi16[i];
    return yazi16['?' - 32];
}

/* Tek baytlik ISO-8859-9: Latin-1'den farkli alti harf Turkce'dir. */
static uint32_t latin5(uint32_t b) {
    switch (b) {
    case 0xD0: return 0x011E;   /* Ğ */
    case 0xDD: return 0x0130;   /* İ */
    case 0xDE: return 0x015E;   /* Ş */
    case 0xF0: return 0x011F;   /* ğ */
    case 0xFD: return 0x0131;   /* ı */
    case 0xFE: return 0x015F;   /* ş */
    default:   return b;
    }
}

/* Glifi (x,y)'ye cizer. saydam: arka renk yerine o anki piksel kullanilir. */
static void glif_ciz(int x, int y, uint32_t kod, uint32_t fg, uint32_t bg, int saydam) {
    const uint8_t *g = fb_glif(kod);
    for (int r = 0; r < YAZI_YUK; r++) {
        int yy = y + r;
        if (yy < cy0 || yy >= cy1) continue;
        for (int c = 0; c < YAZI_EN; c++) {
            int xx = x + c;
            if (xx < cx0 || xx >= cx1) continue;
            uint32_t a = g[r * YAZI_EN + c];
            if (saydam) {
                if (a == 0) continue;
                fb_put_pixel(xx, yy, a == 255 ? fg : fb_karistir(fg, fb_get_pixel(xx, yy), a));
            } else {
                fb_put_pixel(xx, yy, a == 0 ? bg : a == 255 ? fg : fb_karistir(fg, bg, a));
            }
        }
    }
}

void fb_kod_ciz(int x, int y, uint32_t kod, uint32_t fg, uint32_t bg) {
    glif_ciz(x, y, kod, fg, bg, 0);
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    glif_ciz(x, y, latin5((uint8_t)c), fg, bg, 0);
}

static void dize_ciz(int x, int y, const char *s, uint32_t fg, uint32_t bg, int saydam) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { y += FB_SATIR_YUK; cx = x; s++; continue; }
        uint32_t k = fb_utf8_kod(&s);
        glif_ciz(cx, y, k, fg, bg, saydam);
        cx += YAZI_EN;
    }
}

void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    dize_ciz(x, y, s, fg, bg, 0);
}

/* Zemin boyanmadan (kenar yumusatmasi alttaki piksellerle karisir). */
void fb_yazi_saydam(int x, int y, const char *s, uint32_t fg) {
    dize_ciz(x, y, s, fg, 0, 1);
}

/* Ekranda kac hucre kaplar (UTF-8 harf sayisi). */
int fb_yazi_en(const char *s) {
    int n = 0;
    while (*s) { if (*s == '\n') break; (void)fb_utf8_kod(&s); n++; }
    return n * YAZI_EN;
}

static void u32_to_dec(uint32_t v, char *out) {
    char tmp[12];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

void fb_panic(uint32_t code) {
    fb_kirp_sifirla();
    fb_fill_rect(0, 0, (int)fb_w, 40, 0x990000);
    fb_draw_string(16, 12, "KERNEL PANIC - EXCEPTION #", 0xFFFFFF, 0x990000);
    char numbuf[12];
    u32_to_dec(code, numbuf);
    fb_draw_string(16 + 26 * YAZI_EN, 12, numbuf, 0xFFFFFF, 0x990000);
    fb_sun();                  /* panikte kilit beklenmez */
}

/* =====================================================================
 * Asama 42 eklentileri (tema/medya katmani icin)
 * ===================================================================== */

/* UTF-8 kod noktasi cozucu. Agacta IKI kodlama birden var: eski
 * cekirdek dizeleri ISO-8859-9 (tek bayt), yeni dosyalar UTF-8.
 * Devam bayti gelmiyorsa bayt ISO-8859-9 sayilir. */
uint32_t fb_utf8_kod(const char **p) {
    const uint8_t *u = (const uint8_t *)*p;
    if (u[0] < 0x80) { (*p)++; return u[0]; }
    if ((u[1] & 0xC0) != 0x80) { (*p)++; return latin5(u[0]); }
    if ((u[0] & 0xE0) == 0xC0) {
        uint32_t k = (uint32_t)((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        *p += 2; return k;
    }
    if ((u[0] & 0xF0) == 0xE0) {
        if ((u[2] & 0xC0) != 0x80) { (*p)++; return latin5(u[0]); }
        uint32_t k = (uint32_t)((u[0] & 0x0F) << 12) |
                     (uint32_t)((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        *p += 3; return k;
    }
    (*p)++; return latin5(u[0]);
}

/* Hizli satir yazimi/okumasi: piksel basina fonksiyon cagrisi
 * 1920x1080'de kare hizini cokertiyordu. */
void fb_satir_yaz(int x, int y, const uint32_t *kaynak, int adet) {
    for (int i = 0; i < adet; i++) fb_put_pixel(x + i, y, kaynak[i]);
}

void fb_satir_oku(int x, int y, uint32_t *hedef, int adet) {
    for (int i = 0; i < adet; i++) hedef[i] = fb_get_pixel(x + i, y);
}
