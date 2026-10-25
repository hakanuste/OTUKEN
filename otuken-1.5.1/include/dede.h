/* DEDE — Dede Korkut'un sesi: Turkce formant sentezleyici.
 * Kaynak-suzgec modeli: girtlak darbesi ya da gurultu, uc formant
 * cinlayicisindan gecer. Cikis 48 kHz / 16 bit / iki kanal, dogrudan
 * AC'97 surucusune verilir.
 *
 * Ses kimligi: davudi, tok, yasli bilge erkek. Temel perde 84 Hz,
 * formantlar %8 pes (uzun ses yolu), hafif titresim ve soluk gurultusu.
 *
 * Bu bir sinir agi sentezleyicisi DEGILDIR: anlasilir ama mekanik bir
 * ses uretir. Turkce yazildigi gibi okundugu icin harf tabanli esleme
 * tutarli sonuc verir. */
#ifndef OTUKENOS_DEDE_H
#define OTUKENOS_DEDE_H

#include <stdint.h>

#define DEDE_TEMEL_PERDE 84      /* Hz — bilincli olarak pes */

/* Metni seslendirir. Ses aygiti yoksa -1 doner. */
int dede_konus(const char *metin);
/* Metindeki ses birimi sayisi (bosluklar haric). Sinama icin. */
int dede_birim_sayisi(const char *metin);
/* Verilen kod noktasi Turkce sesli mi. */
int dede_sesli_mi(uint32_t kod);
#endif
