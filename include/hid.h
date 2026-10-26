#ifndef OTUKENOS_HID_H
#define OTUKENOS_HID_H
#include <stdint.h>
/* 1.4.9: USB HID raporlari -> klavye kuyrugu ve fare durumu (src/hid.c) */

/* Onyukleme (boot) klavye raporu, 8 bayt */
void     hid_klavye_rapor(const uint8_t *r, int n);
void     hid_tik(void);                 /* tus yinelemesi; ana dongu her tik */
uint32_t hid_klavye_rapor_sayisi(void);

/* Rapor tanimlayicisindan cikarilan fare/tablet yerlesimi */
typedef struct {
    int      gecerli;
    uint32_t rapor_kimligi;             /* 0: kimliksiz */
    int      goreli;                    /* 1 fare, 0 tablet */
    int      dugme_var, eksen_var;
    uint32_t dugme_konum[3];
    uint32_t eksen_konum[2], eksen_boy[2], eksen_azami[2];
} hid_fare_t;
int      hid_fare_cozumle(const uint8_t *tanim, int n, hid_fare_t *f);
void     hid_fare_rapor(const hid_fare_t *f, const uint8_t *r, int n);
uint32_t hid_fare_rapor_sayisi(void);
#endif
