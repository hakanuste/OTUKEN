/* =====================================================================
 * ÜLGEN sahnesi — masaüstü manzarası ve tıklanabilir tamga rıhtımı.
 *
 * Manzara (gece göğü, ay, katmanlı dağlar, su) prosedüreldir: hiçbir
 * görsel saklanmaz, her şey çizim anında hesaplanır. Rıhtımdaki 15
 * tamga vektör tarayıcıyla çizilir ve fare ile tıklanabilir.
 * ===================================================================== */
#ifndef OTUKENOS_ULGEN_THEME_H
#define OTUKENOS_ULGEN_THEME_H

#include "tamga.h"

#define ULGEN_UST_BAR   40
#define ULGEN_RIHTIM_H  400   /* iki sıra x 180 piksel + paylar */

void        ulgen_theme_init(void);
/* Duvar kâğıdı/tema değişince durağan katmanı yeniden bestelemeye zorlar. */
void        ulgen_theme_tazele(void);
/* Uygulamalar ızgarasını açar/kapatır. */
void        ulgen_theme_izgara_degis(void);
int         ulgen_theme_izgara_acik(void);
/* Izgara açıkken bir tamgaya tıklandıysa kimliği, değilse -1. */
int         ulgen_theme_izgara_hit(int x, int y);
void        ulgen_theme_draw(void);
/* Verilen ekran noktası rıhtımdaki bir tamgaya düşüyorsa kimliğini,
 * düşmüyorsa -1 döner. */
int         ulgen_theme_dock_hit(int x, int y);
const char *ulgen_theme_name(int id);
const char *ulgen_theme_role(int id);
#endif
