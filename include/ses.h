#ifndef OTUKENOS_SES_H
#define OTUKENOS_SES_H
#include <stdint.h>

/* --- AC'97 ses surucusu ---
 * Kapsam: PCI'dan denetleyiciyi bul, kodegi baslat, ses tampon
 * tanimlayici listesini (BDL) kur ve PCM ornek calistir.
 * Bicim: 16-bit imzali, 2 kanal (stereo), 48 kHz - AC'97'nin
 * yerel bicimi; yeniden orneklemeye gerek yok. */

#define SES_ORNEK_HIZ 48000
#define SES_KANAL     2

typedef struct {
    int      var;
    uint16_t nam_taban;      /* karistiricI (mixer) I/O tabani */
    uint16_t nabm_taban;     /* veri yolu yonetimi I/O tabani */
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t calinan_ornek;
    int      caliyor;
    int      hda;            /* 1.4.9: 1 = Intel HDA (AC'97 yok) */
} ses_denetleyici_t;

int  ses_init(void);
int  ses_var(void);
int  ses_bosaldi(void);
void ses_bitir(void);              /* 1.4.9: kuyruk bitince durdur (keserek degil) */            /* 1.4.9: kuyruk calinip bitti mi */
const ses_denetleyici_t *ses_durum(void);

/* PCM veriyi (16-bit stereo, 48kHz) calma kuyruguna verir.
 * ornek_sayisi = KARE sayisi (her kare = sol+sag = 4 bayt).
 * Basari: 0, hata: -1 */
int  ses_cal(const int16_t *pcm, uint32_t kare_sayisi);
/* 1.5: beklemeden calma kuyruguna verir; alinan KARE sayisi (0: halka
 * dolu), aygit yoksa -1. */
int  ses_cal_dene(const int16_t *pcm, uint32_t kare_sayisi);

/* Calmayi durdurur. */
void ses_dur(void);

/* Ana ses seviyesi 0..100 */
void ses_seviye(uint8_t yuzde);

/* Sinama amacli: verilen frekansta kare dalga uretip calar. */
int  ses_ton(uint32_t frekans_hz, uint32_t sure_ms);

#endif
