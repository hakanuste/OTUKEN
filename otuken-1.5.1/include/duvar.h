/* =====================================================================
 * DUVAR — ÖTÜKENOS masaüstü duvar kâğıdı.
 *
 * 1024x624, 256 renk indeksli, çekirdeğe gömülü. RLE denendi ve
 * BIRAKILDI: fotoğrafik dokuda sıkışma olmuyor, RLE ham veriden büyük
 * çıkıyordu (700 KB'a karşı 624 KB). Ham indeks dizisi hem küçük hem
 * de sabit maliyetle çizilir.
 *
 * Ekran daha büyükse ölçekleyerek, küçükse kırparak çizilir.
 * ===================================================================== */
#ifndef OTUKENOS_DUVAR_H
#define OTUKENOS_DUVAR_H

#include <stdint.h>

#define DUVAR_EN   1920
#define DUVAR_BOY  1080

extern const uint32_t duvar_palet[256];
extern const uint8_t  duvar_veri[DUVAR_EN * DUVAR_BOY];

/* Duvar kâğıdını (x,y) noktasından başlayarak en x boy alana çizer.
 * Kaynak, hedef alana en-boy oranı korunmadan ölçeklenir (tam ekran). */
void duvar_ciz(int x, int y, int en, int boy);
#endif
