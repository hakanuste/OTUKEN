/* KANAL — giriş/çıkış taşıyıcıları. Hepsi umay_ileti_t üretir. */
#ifndef UMAY_KANAL_H
#define UMAY_KANAL_H
#include "umay.h"
#include "umay_port.h"

typedef struct umay_kanal {
    char        ad[UMAY_AD_MAX];
    kanal_tur_t tur;
    int  (*ac)(struct umay_kanal *k);
    int  (*yokla)(struct umay_kanal *k, umay_ileti_t *i);   /* 0=boş 1=var */
    int  (*yaz)(struct umay_kanal *k, const char *metin);
    void (*kapat)(struct umay_kanal *k);
    void *ic;
    int   acik;
} umay_kanal_t;

umay_kanal_t *kanal_klavye(void);
umay_kanal_t *kanal_ipc(const char *port_adi);
umay_kanal_t *kanal_ses(void);         /* ses/ses.c — bugün deposu boş */
#endif
