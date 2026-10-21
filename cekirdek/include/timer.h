#ifndef OTUKENOS_TIMER_H
#define OTUKENOS_TIMER_H

#include <stdint.h>

void timer_init(uint32_t frequency_hz);
uint64_t timer_get_ticks(void);

#endif
