#ifndef OTUKENOS_GECIKME_H
#define OTUKENOS_GECIKME_H
#include <stdint.h>
/* PIT kanal 2 yoklamasiyla mikrosaniye gecikme. Kesmelerden ve
 * zamanlayici tik'inden bagimsizdir (sti oncesinde de calisir).
 * Kanal 2 (hoparlor kapisi) baska yerde kullanilmiyor.
 * SMP'de: ayni anda iki cekirdek cagirmamali (port 0x61/0x42 paylasimli). */
void pit2_bekle_us(uint32_t us);
#endif
