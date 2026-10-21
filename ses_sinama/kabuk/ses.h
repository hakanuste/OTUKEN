/* Ana bilgisayar kabugu: cekirdegin ses.h'inin yerine gecer.
 * AMAC: ot/src/dede.c'yi DEGISTIRMEDEN derleyip ciktisini WAV'a yazmak.
 * Boylece olculen ses, cekirdekte calisan kodun ta kendisidir; ayri bir
 * Python kopyasi tutulmaz (o kopya bir kez sapmis ve yanlis olcume yol
 * acmisti). */
#ifndef KABUK_SES_H
#define KABUK_SES_H
#include <stdint.h>
int  ses_var(void);
int  ses_cal(const int16_t *pcm, uint32_t kare_sayisi);
void ses_dur(void);
#endif
