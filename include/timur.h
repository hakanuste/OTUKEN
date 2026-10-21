#ifndef OTUKENOS_TIMUR_H
#define OTUKENOS_TIMUR_H

#include <stdint.h>

/* TIMUR - otukenOS guvenlik altyapisi.
 * Bu asamada: NX (No-Execute) destegi, W^X politikasi uygulama,
 * ve bellek koruma dogrulamalari. */

/* CPU'nun NX (calistirilamaz sayfa) destegini sinar ve EFER.NXE
 * bitini acar. Basari:0, desteklenmiyorsa:-1 */
int timur_enable_nx(void);

int timur_nx_available(void);

/* Bir sanal adresi "yazilabilir ama CALISTIRILAMAZ" yapar (veri
 * sayfalari icin - W^X politikasinin W tarafi). */
int timur_protect_data(uint64_t virt);

/* Bir sanal adresi "calistirilabilir ama YAZILAMAZ" yapar (kod
 * sayfalari icin - W^X politikasinin X tarafi). */
int timur_protect_code(uint64_t virt);

/* Verilen adresin mevcut koruma bayraklarini raporlar. */
void timur_report_protection(const char *label, uint64_t virt);

#endif
