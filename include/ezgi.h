/* EZGİ — WAV çalar. Yalnız sıkıştırılmamış PCM (8/16 bit, 1-2 kanal).
 * Kaynak hızı 48 kHz'e, tek kanal iki kanala dönüştürülür.
 * MP3/FLAC/Vorbis ve kap formatları YOKTUR. */
#ifndef OTUKENOS_EZGI_H
#define OTUKENOS_EZGI_H

#include <stdint.h>

typedef struct {
    uint16_t bicim;        /* 1 = PCM        */
    uint16_t kanal;
    uint32_t hz;
    uint16_t bit;
    uint32_t veri_ofset;   /* 'data' yiginin govdesi */
    uint32_t veri_boyu;
} ezgi_bilgi_t;

/*  0 tamam, -1 bicim taninmadi, -2 desteklenmeyen PCM */
int ezgi_basligi_coz(const uint8_t *veri, uint32_t boy, ezgi_bilgi_t *bilgi);
int ezgi_cal_bellekten(const uint8_t *veri, uint32_t boy);
int ezgi_cal(const char *yol);
#endif
