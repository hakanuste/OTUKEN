/* =====================================================================
 * ÜLGEN sahnesi — masaüstü manzarası ve tıklanabilir tamga rıhtımı.
 *
 * Duvar kâğıdı (duvar.c, 1920x1080 indeksli), masaüstü simgeleri, üst
 * çubuk ve 7 tamgalı rıhtım. 1.4.8'de birleştiriciye bağlandı (ekran.c):
 * durağan katman bir kez çizilip bellekte tutulur, üst çubuk her zaman
 * üsttedir. Rıhtım ve simgeler fare ile tıklanabilir (gui.c).
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
/* 1.4.8 birleştirici katmanları */
void        ulgen_theme_arka_ciz(void);        /* durağan: duvar, simgeler, rıhtım */
void        ulgen_theme_ust_bar_ciz(void);     /* her zaman üstte */
void        ulgen_theme_saat_sabitle(int saniye);   /* -1: canlı; sınama için sabit (UTC epoch saniyesi) */
int         ulgen_theme_simge_hit(int x, int y);
int         ulgen_theme_rihtim_konum(int k, int *x, int *y, int *boy);
/* Verilen ekran noktası rıhtımdaki bir tamgaya düşüyorsa kimliğini,
 * düşmüyorsa -1 döner. */
int         ulgen_theme_dock_hit(int x, int y);
/* Üst çubuk: ÖTÜKEN tamgası ULGEN_MENU_TAMGA, menü sözcükleri 0..5
 * (Dosya, Düzen, Görünüm, Uygulamalar, Sistem, Yardım), boşluk -1. */
#define ULGEN_MENU_TAMGA   100
#define ULGEN_MENU_UYGULAMALAR 3
#define ULGEN_MENU_PAKET 100   /* uyg_menu_hit: 100+i = i. kurulu paket uygulamasi */
#define ULGEN_MENU_PAKET_SAYFA (-3)  /* uyg_menu_hit: "sonraki sayfa" satiri */
void ulgen_theme_uyg_menu_sonraki(void);   /* paket listesinde sonraki sayfa (sona gelince basa) */
/* 1.4.9: "Uygulamalar" açılır menüsü (tamga kimliğiyle) */
int  ulgen_theme_uyg_menu_acik(void);
void ulgen_theme_uyg_menu_ata(int acik);
int  ulgen_theme_uyg_var(int id);
void ulgen_theme_uyg_menu_kutu(int *x, int *y, int *w, int *h);
int  ulgen_theme_uyg_menu_hit(int x, int y);   /* tamga / -2 içeride boş / -1 dışarıda */
void ulgen_theme_uyg_menu_ciz(void);
#define ULGEN_MENU_GORUNUM 2
#define ULGEN_MENU_SISTEM  4
int         ulgen_theme_menu_hit(int x, int y);
const char *ulgen_theme_name(int id);
const char *ulgen_theme_role(int id);
#endif
