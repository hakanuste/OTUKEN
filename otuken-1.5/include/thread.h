#ifndef OTUKENOS_THREAD_H
#define OTUKENOS_THREAD_H

#include <stdint.h>
#include "sched.h"

/* "Kernel Thread": bir surece (process) bagli olmadan, dogrudan
 * cekirdek icinde calisan hafif bir yurutme birimi. Mekanik olarak
 * sched.c'deki task_t uzerine ince bir isimlendirme/TID katmanidir. */

void thread_manager_init(void);

task_t *thread_create(const char *name, void (*entry)(void));

/* Su an calisan thread'i 'ticks' kadar uyutur (bkz. task_sleep). */
void thread_sleep(uint64_t ticks);

void thread_wake(task_t *thread);

/* Su an calisan thread'i sonlandirir - ASLA GERI DONMEZ. */
void thread_exit(void);

#endif
