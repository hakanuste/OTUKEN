/* =====================================================================
 * UMAY yasam dongusu — kabuk ile cekirdek arasindaki yuzey.
 *
 * NEDEN AYRI BASLIK: main() bir sure cekirdegin icindeydi ve orada
 * printf kullaniyordu. Cekirdek platform-bagimsiz kalmali; ÖTÜKEN'de
 * stdio yoktur. Giris noktasi artik kabuk/umay_cli.c'de (ana bilgisayar)
 * ya da port/umay_port_otuken.c'deki _start'ta (ÖTÜKEN).
 * ===================================================================== */
#ifndef UMAY_KABUK_H
#define UMAY_KABUK_H

int  umay_kur(void);       /* bir kez: ayar, kutuphane, motor, ajanlar. 0 = oldu */
void umay_tur(void);        /* bir tur: giris yokla, isle, yanit ver    */
int  umay_kosuyor(void);    /* dongu surecek mi                          */
int  umay_mesgul(void);     /* otagda surmekte olan is var mi            */
void umay_sor(const char *soru);  /* tek soru: komut satirindan          */
void umay_kapan(void);      /* bellegi sakla, kanallari kapat            */

#endif
