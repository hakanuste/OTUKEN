#ifndef OTUKENOS_MUTEX_H
#define OTUKENOS_MUTEX_H

#include <stdint.h>
#include "spinlock.h"
#include "sched.h"

#define MUTEX_MAX_WAITERS 8

typedef struct {
    volatile uint32_t locked;
    task_t *waiters[MUTEX_MAX_WAITERS];
    int waiter_count;
    /* SMP: her nesne KENDI kilidini tasir. Kuresel tek kilit yerine
     * nesne basina kilit, farkli kuyruklarin birbirini bloke
     * etmemesini saglar. */
    spinlock_t kilit;
} mutex_t;

/* Spinlock'tan farki: kilit tutuluyorsa CAGIRANI BLOKE eder (CPU'yu
 * baska gorevlere birakir), meşgul-bekleme (busy-wait/spin) YAPMAZ.
 * Uzun surebilecek kritik bolgeler icin dogru arac budur. */

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

/* Ozyinelemeli gorev kilidi: ayni gorev ic ice alabilir. Uyur; kesme
 * baglaminda ya da spinlock tutarken kullanilmaz. Sifirla baslatilmis
 * statik degisken hazirdir (mutex_init gerekmez). */
typedef struct {
    mutex_t  m;
    task_t  *sahip;
    int      derinlik;
    uint64_t alinma;        /* kac kez (disaridan) alindi - kanit sayaci */
} ozkilit_t;

void ozkilit_al(ozkilit_t *k);
void ozkilit_birak(ozkilit_t *k);
int  ozkilit_bende_mi(ozkilit_t *k);

#endif
