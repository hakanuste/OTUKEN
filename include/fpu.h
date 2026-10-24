#ifndef OTUKENOS_FPU_H
#define OTUKENOS_FPU_H
/* 1.4.9: x87/SSE durumu. Çekirdek SSE kullanmaz (-mno-sse); Ring3
 * programları (UMAY'ın ses motoru) kullanabilsin diye her görevin 512
 * baytlık FXSAVE alanı vardır ve görev değişiminde saklanıp yüklenir. */
#include <stdint.h>
#define FPU_ALAN 512
int  fpu_ac(void);                 /* bu çekirdekte CR0/CR4 + başlangıç durumu; SSE yoksa -1 */
int  fpu_etkin(void);
/* görev alanı: 16 hizalı, başlangıç durumuyla dolu. *ham kfree içindir */
uint8_t *fpu_alan_ayir(void **ham);
static inline void fpu_kaydet(uint8_t *a) { if (a) __asm__ volatile ("fxsave64 (%0)" : : "r"(a) : "memory"); }
static inline void fpu_yukle(const uint8_t *a) { if (a) __asm__ volatile ("fxrstor64 (%0)" : : "r"(a) : "memory"); }
#endif
