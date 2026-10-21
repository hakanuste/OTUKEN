#ifndef OTUKENOS_UHCI_H
#define OTUKENOS_UHCI_H
#include <stdint.h>

/* --- UHCI aktarim katmani ---
 * Asama 28'de yalnizca denetleyici TESPIT ediliyordu; klavye
 * calismiyordu. Burada gercek aktarim (kontrol + kesme transferi)
 * uygulanip USB HID klavye okunur.
 * UHCI secildi: XHCI'ya gore cok daha basit (cerceve listesi + TD/QH)
 * ve USB klavye icin fazlasiyla yeterli. */

int  uhci_init(void);          /* denetleyiciyi bul, cerceve listesini kur */
int  uhci_var(void);
int  uhci_klavye_var(void);
/* Klavyeden bir tus kodu okur (HID kullanim kodu), yoksa 0. */
uint8_t uhci_klavye_oku(void);
void uhci_yokla(void);         /* periyodik cagrilir */

#endif
