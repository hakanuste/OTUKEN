/* =====================================================================
 * SİMGE — ÖTÜKENOS sistem simgeleri (klasör, sabit disk, çöp kutusu).
 *
 * Tamgalardan AYRI bir ailedir: tamgalar vektördür ve tek/çok renkli
 * çizilir; bu simgeler ise fotoğrafik kaplamalı görsellerdir, o yüzden
 * palet + örtme (alfa) taşıyan bitmap olarak saklanır.
 * Piksel başına bir bayt: üst 4 bit palet indeksi, alt 4 bit alfa.
 * ===================================================================== */
#ifndef OTUKENOS_SIMGE_H
#define OTUKENOS_SIMGE_H

#include <stdint.h>
#include "fb.h"

typedef enum {
    SIMGE_KLASOR = 0,
    SIMGE_DISK,
    SIMGE_COP,
    SIMGE_SAYI
} simge_id_t;

typedef struct {
    const char    *ad;
    const char    *is;
    const uint8_t *k32;
    const uint8_t *k48;
    const uint8_t *k64;
    const uint32_t *palet;   /* 15 renk */
} simge_t;

const simge_t *simge_al(simge_id_t id);
/* boy: 32, 48 ya da 64. Zeminle harmanlanarak çizilir. */
void simge_ciz(int x, int y, simge_id_t id, int boy);
#endif
