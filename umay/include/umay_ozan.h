/* OZAN — UMAY'ın dili ve kişiliği. Dede Korkut ağzı.
 * Kanal ne olursa olsun (klavye/GUI/ses) kişilik tektir. */
#ifndef UMAY_OZAN_H
#define UMAY_OZAN_H
#include "umay.h"

void        ozan_kur(const char *kullanici_adi);
const char *ozan_kimlik(void);                       /* US'e verilen kişilik metni */
void        ozan_acilis(char *c, uint32_t n);
void        ozan_hitap(char *c, uint32_t n);
coz_t       ozan_coz(const char *soz);               /* niyet + alan çözümlemesi */
const char *ozan_uzman_kimligi(alan_t a);            /* uzmana özel kimlik metni */
void        ozan_soyle(niyet_t niyet, const char *oz, char *c, uint32_t n);
void        ozan_ogut(const char *konu, char *c, uint32_t n);
#endif
