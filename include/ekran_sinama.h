#ifndef OTUKENOS_EKRAN_SINAMA_H
#define OTUKENOS_EKRAN_SINAMA_H
#include <stdint.h>

/* ÜLGEN/GUI çizim hattı regresyon sınaması (1024x768).
 * Gerçek çizim kodunu (fb.c, gui.c, ulgen.c) belirlenimci bir sahneyle
 * sürer ve sonucu üç bağımsız yoldan denetler:
 *   1) piksel sahipliği: her pikselin beklenen z-sırasındaki en üst
 *      öğenin renk kümesinde olması (masaüstü = tam renk)
 *   2) yazı: glif glif, sınamanın KENDİ harmanlama ve UTF-8 çözümüyle
 *      hesaplanan beklenen piksellerle birebir karşılaştırma
 *   3) ekran belleği: fb_sun sonrası VRAM == arka tampon
 * Hem ana makinede (tests/ulgen_render) hem çekirdekte
 * (-DOTUKEN_EKRAN_SINAMA=1) aynı kod koşar.
 *
 * yaz: satır yazıcı (seri kapı / stdout). Dönüş: başarısız denetim sayısı. */
typedef void (*ekran_yaz_t)(const char *s);
int ekran_sinamasi(ekran_yaz_t yaz);

/* Son sahnenin arka tampon ve VRAM özetleri (FNV-1a 64) */
uint64_t ekran_sinama_ozet(void);
uint64_t ekran_sinama_vram_ozet(void);

#endif
