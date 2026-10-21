/* OZAN — Dede Korkut'un sesi. Konuşma DEĞİL: metni Türkçe hecelerine
 * bölüp kopuz düzeninde ezgiye çevirir ve PC hoparlöründen çalar.
 * Gerçek konuşma PCM çıkışı ve sentezleyici ister; ikisi de yok. */
#ifndef OTUKENOS_OZAN_SES_H
#define OTUKENOS_OZAN_SES_H

/* Metni heceleyip ezgiyle "söyler". hiz: 1 ağır, 2 orta, 3 hızlı. */
void ozan_soyle(const char *metin, int hiz);
/* Dede Korkut'un giriş kopuzu. */
void ozan_selam(void);
/* Türkçe hece sayısı (sesli harf sayısı). */
int  ozan_hece_say(const char *metin);
#endif
