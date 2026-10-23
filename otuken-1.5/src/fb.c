#include "fb.h"
#include "font8x8.h"

static uint8_t  *fb_addr;
static uint32_t  fb_pitch;
static uint32_t  fb_w, fb_h;
static uint8_t   fb_bpp;
static uint8_t   red_pos, red_size, green_pos, green_size, blue_pos, blue_size;

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

    (void)red_size; (void)green_size; (void)blue_size;
}

int fb_width(void)  { return (int)fb_w; }
int fb_height(void) { return (int)fb_h; }

/* rgb: 0x00RRGGBB seklinde standart 24-bit renk; framebuffer'in
 * gercek bit duzenine (color_info) gore paketleyip yaziyoruz. */
void fb_put_pixel(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_w || (uint32_t)y >= fb_h)
        return;

    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8)  & 0xFF;
    uint8_t b =  rgb        & 0xFF;

    uint32_t packed = ((uint32_t)r << red_pos) |
                       ((uint32_t)g << green_pos) |
                       ((uint32_t)b << blue_pos);

    uint8_t *pixel = fb_addr + y * fb_pitch + x * (fb_bpp / 8);

    if (fb_bpp == 32) {
        *(uint32_t *)pixel = packed;
    } else if (fb_bpp == 24) {
        pixel[0] = (packed) & 0xFF;
        pixel[1] = (packed >> 8) & 0xFF;
        pixel[2] = (packed >> 16) & 0xFF;
    } else if (fb_bpp == 16) {
        *(uint16_t *)pixel = (uint16_t)packed;
    }
}

uint32_t fb_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_w || (uint32_t)y >= fb_h)
        return 0;

    uint8_t *pixel = fb_addr + y * fb_pitch + x * (fb_bpp / 8);
    uint32_t packed = 0;

    if (fb_bpp == 32) {
        packed = *(uint32_t *)pixel;
    } else if (fb_bpp == 24) {
        packed = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16);
    } else if (fb_bpp == 16) {
        packed = *(uint16_t *)pixel;
    }

    uint32_t r = (packed >> red_pos)   & ((1u << red_size)   - 1);
    uint32_t g = (packed >> green_pos) & ((1u << green_size) - 1);
    uint32_t b = (packed >> blue_pos)  & ((1u << blue_size)  - 1);

    /* 5/6-bit kanallari kabaca 8-bit'e olcekle (16bpp durumunda) */
    if (fb_bpp == 16) {
        r = (r * 255) / ((1 << red_size) - 1);
        g = (g * 255) / ((1 << green_size) - 1);
        b = (b * 255) / ((1 << blue_size) - 1);
    }

    return (r << 16) | (g << 8) | b;
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_put_pixel(x + i, y + j, rgb);
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb) {
    for (int i = 0; i < w; i++) {
        fb_put_pixel(x + i, y, rgb);
        fb_put_pixel(x + i, y + h - 1, rgb);
    }
    for (int j = 0; j < h; j++) {
        fb_put_pixel(x, y + j, rgb);
        fb_put_pixel(x + w - 1, y + j, rgb);
    }
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x8[(uint8_t)c];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (1 << (7 - col))) ? fg : bg;
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (*s) {
        if (*s == '\n') {
            y += 10;
            cx = x;
        } else {
            fb_draw_char(cx, y, *s, fg, bg);
            cx += 8;
        }
        s++;
    }
}

static void u32_to_dec(uint32_t v, char *out) {
    char tmp[12];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

void fb_panic(uint32_t code) {
    fb_fill_rect(0, 0, (int)fb_w, 40, 0x990000);
    fb_draw_string(16, 16, "KERNEL PANIC - EXCEPTION #", 0xFFFFFF, 0x990000);
    char numbuf[12];
    u32_to_dec(code, numbuf);
    fb_draw_string(16 + 26 * 8, 16, numbuf, 0xFFFFFF, 0x990000);
}

/* =====================================================================
 * Asama 42 eklentileri (tema/medya katmani icin)
 * ===================================================================== */

/* UTF-8 kod noktasi cozucu. Agacta IKI kodlama birden var: eski
 * cekirdek dizeleri ISO-8859-9 (tek bayt), yeni dosyalar UTF-8.
 * Devam bayti gelmiyorsa bayt dogrudan kullanilir. */
uint32_t fb_utf8_kod(const char **p) {
    const uint8_t *u = (const uint8_t *)*p;
    if (u[0] < 0x80) { (*p)++; return u[0]; }
    if ((u[1] & 0xC0) != 0x80) { (*p)++; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0) {
        uint32_t k = (uint32_t)((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        *p += 2; return k;
    }
    if ((u[0] & 0xF0) == 0xE0) {
        if ((u[2] & 0xC0) != 0x80) { (*p)++; return u[0]; }
        uint32_t k = (uint32_t)((u[0] & 0x0F) << 12) |
                     (uint32_t)((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        *p += 3; return k;
    }
    (*p)++; return u[0];
}

/* Hizli satir yazimi/okumasi: piksel basina fonksiyon cagrisi
 * 1920x1080'de kare hizini cokertiyordu. */
void fb_satir_yaz(int x, int y, const uint32_t *kaynak, int adet) {
    for (int i = 0; i < adet; i++) fb_put_pixel(x + i, y, kaynak[i]);
}

void fb_satir_oku(int x, int y, uint32_t *hedef, int adet) {
    for (int i = 0; i < adet; i++) hedef[i] = fb_get_pixel(x + i, y);
}
