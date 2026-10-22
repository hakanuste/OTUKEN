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
/* 1.4.9 sinama: son konusmanin karta verilen kare ve birim sayisi */
uint32_t dede_son_kare(void);
uint32_t dede_son_birim(void);
/* 1.4.9: eşzamansız konuşma. dede_sira_baslat ses_init'ten sonra bir kez.
 * dede_konus_sirala: metni sıraya koyar ve HEMEN döner (0), ses yoksa -1.
 * Sıra (4) doluysa en eski bekleyen düşer. */
void dede_sira_baslat(void);
int  dede_konus_sirala(const char *metin);
int  dede_mesgul(void);          /* sırada ya da söylenmekte olan var mı */
uint32_t dede_dusen(void);       /* sıra dolu olduğu için düşen metin */
#endif
