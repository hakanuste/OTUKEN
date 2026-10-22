#ifndef OTUKENOS_FB_H
#define OTUKENOS_FB_H

#include <stdint.h>
#include "multiboot.h"

void fb_init(multiboot_info_t *mbi);
void fb_put_pixel(int x, int y, uint32_t rgb);
uint32_t fb_get_pixel(int x, int y);
void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb);
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg);
int  fb_width(void);
int  fb_height(void);
void fb_panic(uint32_t code);

/* 1.4.8: yazi 8x16 (kenar yumusatmali), satir yuksekligi 16 */
#define FB_YAZI_EN    8
#define FB_YAZI_YUK   16
#define FB_SATIR_YUK  16
void     fb_yazi_saydam(int x, int y, const char *s, uint32_t fg);
int      fb_yazi_en(const char *s);
uint32_t fb_karistir(uint32_t on, uint32_t zemin, uint32_t a);
const uint8_t *fb_glif(uint32_t kod);

/* Kirpma dikdortgeni: [x0,x1) x [y0,y1). Butun ilkeller uyar. */
typedef struct { int x0, y0, x1, y1; } fb_kirp_t;
void      fb_kirp_sifirla(void);
fb_kirp_t fb_kirp_al(void);
void      fb_kirp_geri(fb_kirp_t k);
void      fb_kirp_daralt(int x, int y, int w, int h);
void      fb_blit(int x, int y, int w, int h, const uint32_t *kaynak, int adim);
void      fb_kod_ciz(int x, int y, uint32_t kod, uint32_t fg, uint32_t bg);

/* Cift tampon: heap hazir olunca kur; cizimleri fb_sun() ekrana aktarir
 * (cagiran gorsel_kilit tutmali). */
int      fb_arka_tampon_kur(void);
int      fb_arka_tampon_var(void);
void     fb_sun(void);
uint32_t fb_ekran_pikseli(int x, int y);   /* sinama: VRAM okuma */
uint32_t fb_nicemle(uint32_t rgb);         /* sinama: VRAM'e yazilip okununca */

uint32_t fb_utf8_kod(const char **p);
void     fb_satir_yaz(int x, int y, const uint32_t *kaynak, int adet);
void     fb_satir_oku(int x, int y, uint32_t *hedef, int adet);
#endif
