/* =====================================================================
 * TEMA — ÖTÜKEN görsel kimliği tek kaynaktan.
 *
 * Renkler daha önce gui.c ve ulgen.c içinde AYRI AYRI #define'lanmisti;
 * ikisi birbirinden habersiz degisebiliyordu. Artik tek tema yapisi var
 * ve calisirken degistirilebiliyor (kabuktan: "tema <ad>").
 * ===================================================================== */
#ifndef OTUKENOS_TEMA_H
#define OTUKENOS_TEMA_H

#include <stdint.h>

#define TAMGA_SAYDAM 0xFFFFFFFFu

/* Tamga sistemi ana paleti */
#define TEMA_TURKUAZ  0x00E0FFu
#define TEMA_KREM     0xF3E8D0u
#define TEMA_BEYAZ    0xFFFFFFu
#define TEMA_MERCAN   0xFF5A5Au
#define TEMA_LAVANTA  0xB56CFFu
#define TEMA_OKYANUS  0x0A1F28u
/* Altın/bronz tamamen atılmadı: sıcak krem-metalik olarak mühür
 * çerçevelerinde ve ince ayrıntılarda kullanılır. Baskın değildir. */
#define TEMA_METALIK  0xD4AF37u
#define TEMA_ALTIN    0xFFD166u   /* ASENA, KERVAN: sıcak altın */

typedef struct {
    const char *ad;
    /* yüzeyler */
    uint32_t masaustu;
    uint32_t masaustu_alt;  /* geçiş (gradyan) alt rengi          */
    uint32_t doku;          /* eşkenar dörtgen kafes çizgi rengi  */
    uint32_t gorev_cubugu;
    uint32_t pencere_zemin;
    uint32_t baslik;
    uint32_t baslik_odakli;
    uint32_t kenar;
    /* yazılar */
    uint32_t gorev_yazi;
    uint32_t pencere_yazi;
    uint32_t baslik_yazi;
    /* öğeler */
    uint32_t dugme;
    uint32_t dugme_etkin;
    uint32_t kapat;
    uint32_t imlec;
    uint32_t uyari;
    uint32_t vurgu;
    uint32_t umay;          /* UMAY ve özel dizgeler için lavanta */
    uint32_t metalik;       /* mühür çerçevesi, sıcak krem vurgu    */
    uint32_t altin;         /* sıcak altın vurgu                    */
    uint32_t krem_vurgu;    /* kar sırtı, üst bar başlığı           */
    uint32_t beyaz_yerine_krem; /* parlak yazı                      */
    /* ölçüler */
    int baslik_yuksekligi;
    int kenar_kalinligi;
} tema_t;

void            tema_init(void);
const tema_t   *tema_simdiki(void);
int             tema_sayisi(void);
const tema_t   *tema_sirali(int i);
/* Adına göre temayı uygular; bulunamazsa -1. Uygulanınca ekran
 * tamamen yeniden çizilir. */
int             tema_uygula(const char *ad);
#endif
