#ifndef OTUKENOS_GPT_H
#define OTUKENOS_GPT_H
#include <stdint.h>

/* --- GPT (GUID Bolum Tablosu) ---
 * 2 TB ustu diskler MBR ile bolumlenebilir DEGILDIR: MBR'nin bolum
 * baslangic/uzunluk alanlari 32 bittir ve 2 TB'da tasar. Bu yuzden
 * buyuk SSD'ler (4/8 TB) GPT kullanir.
 *
 * Su ana kadar diskin TAMAMINI tek bir dosya sistemi sayiyorduk.
 * Gercek bir diskte sektor 0 bolum tablosudur; dosya sistemi bir
 * BOLUMUN icinde baslar. Bu katman bolum tablosunu okuyup dosya
 * sistemlerinin dogru ofsette aranmasini saglar. */

#define GPT_AZAMI_BOLUM 16
#define GPT_AD_UZUNLUK  36   /* UTF-16 karakter */

typedef struct {
    int      var;
    uint64_t ilk_lba;
    uint64_t son_lba;        /* dahil */
    uint64_t sektor_sayisi;
    uint8_t  tur_guid[16];
    char     ad[GPT_AD_UZUNLUK + 1];   /* ASCII'ye indirgenmis */
} gpt_bolum_t;

/* Diski tarar. Donus: bulunan bolum sayisi, GPT yoksa 0, hata -1. */
int gpt_tara(int disk_id);

int  gpt_bolum_sayisi(void);
const gpt_bolum_t *gpt_bolum_al(int i);

/* Bolumun dosya sistemi turu taninabiliyor mu (FAT/veri) */
int gpt_veri_bolumu_mu(const gpt_bolum_t *b);

#endif
