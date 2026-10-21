#ifndef OTUKENOS_MUTEX_H
#define OTUKENOS_MUTEX_H

#include <stdint.h>
#include "sched.h"

#define MUTEX_MAX_WAITERS 8

typedef struct {
    volatile uint32_t locked;
    task_t *waiters[MUTEX_MAX_WAITERS];
    int waiter_count;
} mutex_t;

/* Spinlock'tan farki: kilit tutuluyorsa CAGIRANI BLOKE eder (CPU'yu
 * baska gorevlere birakir), meşgul-bekleme (busy-wait/spin) YAPMAZ.
 * Uzun surebilecek kritik bolgeler icin dogru arac budur. */

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif
