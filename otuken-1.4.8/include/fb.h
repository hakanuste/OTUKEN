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

uint32_t fb_utf8_kod(const char **p);
void     fb_satir_yaz(int x, int y, const uint32_t *kaynak, int adet);
void     fb_satir_oku(int x, int y, uint32_t *hedef, int adet);
#endif
