/* =====================================================================
 * TAMGA — ÖTÜKEN simge dizgesi. VEKTÖR tabanlıdır.
 *
 * Simgeler bitmap değil, KAPALI ÇOKGEN YOLLARI olarak saklanır.
 * Koordinatlar 0..1000 birim karesinde, yukarıdan aşağı (SVG düzeni).
 * Çizim anında istenen boya ölçeklenir ve kenar yumuşatmalı taranır;
 * bu yüzden 16 pikselde de 256 pikselde de keskindir.
 *
 * Renk katmanları: her simge en çok 3 tonlu katman taşır (koyudan
 * açığa). Katmanlar sırayla çizilir, üstteki alttakini örter.
 *
 * Tarayıcı tamsayıdır: bu çekirdekte FPU ve SSE kapalıdır.
 * ===================================================================== */
#ifndef OTUKENOS_TAMGA_H
#define OTUKENOS_TAMGA_H

#include <stdint.h>

#define TAMGA_KATMAN   3
#define TAMGA_BIRIM    1000   /* tasarım karesi kenarı */
#define TAMGA_AZAMI    128    /* tarayıcının desteklediği en büyük boy */
/* Bu boyun ALTINDA vektör taranmaz; elle çizilmiş 16x16 maske çizilir.
 * Ölçüm: 16 pikselde vektör çizgileri piksel altına düşüp lekeleşiyor. */
#define TAMGA_KUCUK_SINIR 24

typedef enum {
    TAMGA_OTUKEN = 0, TAMGA_UMAY, TAMGA_DEDEKORKUT, TAMGA_ULGEN, TAMGA_TULPAR,
    TAMGA_TIMUR, TAMGA_KUT, TAMGA_ASENA, TAMGA_ERGENEKON, TAMGA_BITIG,
    TAMGA_ULAK, TAMGA_KERVAN, TAMGA_TORE, TAMGA_TAMGA, TAMGA_ORKUN,
    TAMGA_SAYI
} tamga_id_t;

typedef struct { int16_t x, y; } tv_nokta_t;

typedef struct {
    uint16_t bas;      /* nokta dizisindeki başlangıç */
    uint16_t uzunluk;
    uint8_t  katman;   /* 0 = en koyu, 2 = en açık */
} tv_yol_t;

typedef struct {
    const char       *ad;
    const char       *is;
    const tv_nokta_t *nokta;
    const tv_yol_t   *yol;
    uint16_t          yol_sayi;
    const uint32_t   *palet;   /* TAMGA_KATMAN renk */
    uint32_t          karo;    /* yuvarlatılmış karo zemini */
    uint32_t          vurgu;   /* simgenin palet rengi (karo için) */
} tamga_t;

const tamga_t *tamga_al(tamga_id_t id);
/* Simgenin ÜLGEN paletindeki kendi rengi (UMAY lavanta, TİMUR mercan...). */
uint32_t tamga_vurgu(tamga_id_t id);
/* Rengi aydınlatır (+) ya da karartır (-). -255..255 */
uint32_t tamga_ton(uint32_t renk, int miktar);

/* Simgeyi istenen boyda tarar ve zemine harmanlar.
 * boy: 8..128 arası herhangi bir değer (vektör, boy tablosu yok).
 * renk != 0 ise bütün katmanlar o tek renkte çizilir (tek renkli kip).
 * zemin TAMGA_SAYDAM ise ekrandaki mevcut piksellerle harmanlanır. */
void tamga_ciz(int x, int y, tamga_id_t id, int boy, uint32_t renk, uint32_t zemin);

/* Yuvarlatılmış karo + ortalanmış simge.
 * renk      : simgenin rengi (0 -> tamganın kendi katman renkleri).
 * karo_renk : karonun TABAN tonu; kenarlık ve dolgu bundan türetilir
 *             (0 -> tamganın kendi palet rengi). */
void tamga_kutu_ciz(int x, int y, tamga_id_t id, int boy, int kenar_pay,
                    uint32_t renk, uint32_t karo_renk);
/* 44x44 karo+simge birleşimini önbellekten çizer (rıhtım için).
 * İlk çağrıda tarar, sonra kopyalar. */
void tamga_kutu_onbellekli(int x, int y, tamga_id_t id, uint32_t renk,
                           uint32_t karo_renk);
void tamga_onbellek_bosalt(void);

/* Dairesel mühür. YALNIZ büyük sistem amblemlerinde. boy önerilen: 64..96 */
void tamga_muhur_ciz(int x, int y, tamga_id_t id, int boy,
                     uint32_t renk, uint32_t zemin);

void tamga_yuvarlak_kutu(int x, int y, int en, int boy, int yaricap, uint32_t renk);
#endif
