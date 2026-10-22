#ifndef OTUKENOS_TIMER_H
#define OTUKENOS_TIMER_H

#include <stdint.h>

void timer_init(uint32_t frequency_hz);
uint64_t timer_get_ticks(void);
/* PIT tik frekansi (Hz). Kullanici alani zaman birimini bundan turetir
 * (SYS_SISTEM_BIL / SYSBIL_TIK_HZ): UMAY eskiden tiki 10 ms sayiyordu,
 * cekirdek 50 Hz'de (20 ms) calisiyor -> saati yari hizda isliyordu. */
uint32_t timer_hz(void);

#endif
