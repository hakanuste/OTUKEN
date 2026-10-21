/* =====================================================================
 * TAMGA GÖRSELLERİ — ULGEN_THEME_ASSETS.zip'teki kesin tasarım referansı.
 *
 * Bu ikonlar YENİDEN ÇİZİLMEZ. Vektör tamga dizgesi (tamga.h) hâlâ
 * durur ve küçük boy / tek renk gereken yerlerde kullanılır; rıhtım ve
 * büyük gösterimler bu görselleri kullanır.
 *
 * Sıra README.txt ile birebir aynıdır ve DEĞİŞTİRİLEMEZ:
 *  0 ÖTÜKEN  1 UMAY  2 DEDE KORKUT  3 ÜLGEN  4 TULPAR  5 TİMUR
 *  6 KUT  7 ASENA  8 ERGENEKON  9 BİTİG  10 ULAK  11 KERVAN
 * 12 TÖRE  13 TAMGA  14 ORKUN
 * ===================================================================== */
#ifndef OTUKENOS_TAMGA_GORSEL_H
#define OTUKENOS_TAMGA_GORSEL_H

#include <stdint.h>
#include "fb.h"

#define TAMGA_GORSEL_SAYI 15

typedef struct {
    const uint8_t  *k48;
    const uint8_t  *k64;
    const uint8_t  *k96;
    const uint8_t  *k128;    /* rıhtımda büyük gösterim */
    const uint32_t *palet;   /* 256 renk — logolar fotoğrafik */
} tamga_gorsel_t;

extern const tamga_gorsel_t TAMGA_GORSEL[TAMGA_GORSEL_SAYI];

/* boy: 48, 64, 96 ya da 128. Logolar SİYAH zeminlidir ve OPAK çizilir;
 * rıhtım da siyah olduğu için dikiş görünmez. parlaklik 0..255 — hover için aydınlatma. */
void tamga_gorsel_ciz(int x, int y, int id, int boy, int parlaklik);
#endif
