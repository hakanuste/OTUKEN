#ifndef OTUKENOS_HDA_H
#define OTUKENOS_HDA_H
#include <stdint.h>
/* 1.4.9: Intel HD Audio cikis surucusu (src/hda.c). ses.c AC'97 yoksa bunu
 * kullanir; bicim 48 kHz, 16 bit, stereo (ses.h ile ayni). */
int      hda_init(void);
int      hda_var(void);
int      hda_cal(const int16_t *pcm, uint32_t kare);      /* bekleyerek: 0 / -1 */
int      hda_cal_dene(const int16_t *pcm, uint32_t kare); /* beklemeden: alinan kare */
int      hda_bosaldi(void);
void     hda_dur(void);
void     hda_seviye(uint8_t yuzde);
uint16_t hda_vendor(void);
uint16_t hda_aygit(void);
#endif
