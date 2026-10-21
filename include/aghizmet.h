#ifndef OTUKENOS_AGHIZMET_H
#define OTUKENOS_AGHIZMET_H

#include <stdint.h>

/* --- Asama 38: ÖTÜKEN Ag Hizmetleri ---
 * Soket katmani uzerine kurulu, GELEN istekleri karsilayan sunucu
 * hizmetleri:
 *   - YANKI (echo)   : aldigini geri gonderir      (UDP 7)
 *   - ZAMAN (daytime): sistem zamanini bildirir    (UDP 13)
 *   - DURUM          : sistem durum ozeti dondurur (UDP 1900)
 */

#define AGHIZMET_YANKI_PORT 7
#define AGHIZMET_ZAMAN_PORT 13
#define AGHIZMET_DURUM_PORT 1900

void aghizmet_init(void);

/* Gelen datagramlari isler; ana dongu/servis tarafindan periyodik
 * cagirilir. Islenen istek sayisini dondurur. */
int  aghizmet_isle(void);

uint32_t aghizmet_istek_sayisi(void);
uint32_t aghizmet_yanit_sayisi(void);

#endif
