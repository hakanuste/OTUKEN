#include "mutex.h"

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->waiter_count = 0;
}

void mutex_lock(mutex_t *m) {
    cli();
    if (!m->locked) {
        m->locked = 1;
        sti();
        return; /* bos kilidi hemen aldik */
    }

    /* Kilitli: kendimizi bekleme kuyruguna ekleyip bloke oluyoruz.
     * ONEMLI: task_block_and_switch'i cli() TUTULURKEN degil, kendi
     * cli/sti cifti ayri calissin diye burada sti() cagirdiktan SONRA
     * cagiriyoruz - ic ice (nested) cli/sti'den bilerek kaciniyoruz,
     * cunku ic fonksiyonun sti()'si disaridakini erken acabilirdi. */
    task_t *self = scheduler_current_task();
    if (m->waiter_count < MUTEX_MAX_WAITERS) {
        m->waiters[m->waiter_count++] = self;
    }
    sti();

    task_block_and_switch();
    /* Buraya donduysek, mutex_unlock kilidi DOGRUDAN bize devretti
     * (locked hala 1) - baska bir sey yapmaya gerek yok. */
}

void mutex_unlock(mutex_t *m) {
    task_t *to_wake = 0;

    cli();
    if (m->waiter_count > 0) {
        to_wake = m->waiters[0];
        for (int i = 1; i < m->waiter_count; i++) m->waiters[i - 1] = m->waiters[i];
        m->waiter_count--;
        /* locked=1 OLARAK KALIR - kilit dogrudan to_wake'e devrediliyor,
         * araya baskasi giremesin diye 0'a hic dusurmuyoruz. */
    } else {
        m->locked = 0; /* kimse beklemiyor, gercekten serbest */
    }
    sti();

    if (to_wake) task_unblock(to_wake); /* kendi cli/sti ciftini kullanir */
}
