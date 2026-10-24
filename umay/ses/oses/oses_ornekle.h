/* 22050 Hz tek kanal float -> 48000 Hz ikili kanal int16 (Catmull-Rom).
 * UMAY'ın Dede Korkut sürücüsü ve ana makinedeki kanıt aracı AYNI işlevi kullanır. */
#ifndef OSES_ORNEKLE_H
#define OSES_ORNEKLE_H
#include <stdint.h>
#define OSES_GIRIS_HZ 22050
#define OSES_CIKIS_HZ 48000
/* çıkış kare sayısı (tampon en az bu kadar kare = 4 bayt) */
uint32_t oses_ornekle_boy(long n);
uint32_t oses_ornekle(const float *s, long n, int16_t *c);
#endif
