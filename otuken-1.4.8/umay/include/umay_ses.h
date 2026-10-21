/* SES — ileriye dönük genişletme yüzeyi. Bugün hiçbir sürücü zorunlu değil.
 * Gerçek STT/TTS geldiğinde yalnız ses_surucu_t doldurulur; UMAY değişmez. */
#ifndef UMAY_SES_H
#define UMAY_SES_H
#include "umay.h"

typedef struct ses_surucu {
    char ad[UMAY_AD_MAX];
    int  (*ac)(struct ses_surucu *s, uint32_t ornek_hizi, uint32_t kanal);
    /* STT: ham PCM -> metin. 0 = söz yok */
    int  (*dinle)(struct ses_surucu *s, char *metin, uint32_t n);
    /* TTS: metin -> ses. Kuyruğa atar, bloklamaz */
    int  (*soyle)(struct ses_surucu *s, const char *metin);
    int  (*bosalt)(struct ses_surucu *s);
    void (*kapat)(struct ses_surucu *s);
    void *ic;
} ses_surucu_t;

int  ses_surucu_tak(ses_surucu_t *s);
int  ses_var_mi(void);
const char *ses_surucu_adi(void);
int  ses_dinle(char *metin, uint32_t n);
int  ses_soyle(const char *metin);
ses_surucu_t *ses_bos_surucu(void);   /* her çağrıya UM_DESTEKSIZ döner */
/* Dede Korkut: metni kopuz ezgisine çevirir. Konuşma DEĞİL. */
ses_surucu_t *ses_ozan_surucu(void);
#endif
