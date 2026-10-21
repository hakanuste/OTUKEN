#include "mutex.h"
#include "spinlock.h"

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }

void mutex_init(mutex_t *m) {
    spinlock_init(&m->kilit);
    m->locked = 0;
    m->waiter_count = 0;
}

void mutex_lock(mutex_t *m) {
    for (;;) {
        uint64_t _kb = spin_lock_irqsave(&m->kilit);
        if (!m->locked) {
            m->locked = 1;
            spin_unlock_irqrestore(&m->kilit, _kb);
            task_t *ben = scheduler_current_task();
            if (ben) __atomic_add_fetch(&ben->tutulan_kilit, 1, __ATOMIC_ACQ_REL);
            return; /* bos kilidi hemen aldik */
        }
        task_t *self = scheduler_current_task();
        /* Bekleyen listesi DOLUYSA (ya da zamanlayici henuz yoksa) bloke
         * OLMA: eskiden liste doluyken gorev listeye eklenmeden uyuyordu
         * ve onu uyandiracak kimse olmadigi icin SONSUZA DEK kaliyordu.
         * Islemciyi birakip yeniden dene. */
        if (!self || m->waiter_count >= MUTEX_MAX_WAITERS) {
            spin_unlock_irqrestore(&m->kilit, _kb);
            if (self) scheduler_tick();          /* yield */
            else __asm__ volatile ("pause");
            continue;
        }
        m->waiters[m->waiter_count++] = self;
        spin_unlock_irqrestore(&m->kilit, _kb);

        /* Kilit birakilip bloke olunana kadar gelen uyandirma kaybolmaz:
         * task_unblock jeton birakir (sched.c, uyandir_bekliyor). */
        task_block_and_switch();
        /* mutex_unlock kilidi DOGRUDAN bize devretti (locked hala 1). */
        __atomic_add_fetch(&self->tutulan_kilit, 1, __ATOMIC_ACQ_REL);
        return;
    }
}

void mutex_unlock(mutex_t *m) {
    task_t *to_wake = 0;
    task_t *ben = scheduler_current_task();
    if (ben && __atomic_load_n(&ben->tutulan_kilit, __ATOMIC_ACQUIRE) > 0)
        __atomic_sub_fetch(&ben->tutulan_kilit, 1, __ATOMIC_ACQ_REL);

    uint64_t _kb = spin_lock_irqsave(&m->kilit);
    if (m->waiter_count > 0) {
        to_wake = m->waiters[0];
        for (int i = 1; i < m->waiter_count; i++) m->waiters[i - 1] = m->waiters[i];
        m->waiter_count--;
        /* locked=1 OLARAK KALIR - kilit dogrudan to_wake'e devrediliyor,
         * araya baskasi giremesin diye 0'a hic dusurmuyoruz. */
    } else {
        m->locked = 0; /* kimse beklemiyor, gercekten serbest */
    }
    spin_unlock_irqrestore(&m->kilit, _kb);

    if (to_wake) task_unblock(to_wake); /* kendi cli/sti ciftini kullanir */
}

/* =====================================================================
 * OZYINELEMELI GOREV KILIDI (SMP adim 6)
 * Ic ice cagrilan katmanlar (VFS -> FAT16 -> onbellek -> disk) icin:
 * ayni gorev tekrar alabilir, derinlik sayilir. Uyuyan kilittir:
 * KESME baglaminda ve bir SPINLOCK tutarken ALINMAMALIDIR.
 * Zamanlayici baslamadan once (tek akis) islem yapmaz.
 * ===================================================================== */
#ifdef DEPO_KILIT_KAPALI
#include "depo_kilit.h"
/* YALNIZ SINAMA (kontrol grubu): depolama kilidi devre disi. SMP-YARIS
 * sinamasinin kilidi gercekten olctugunu gostermek icin kullanilir. */
#define KONTROL_GRUBU(k) if ((k) == &depo_kilit) return;
#else
#define KONTROL_GRUBU(k)
#endif

void ozkilit_al(ozkilit_t *k) {
    KONTROL_GRUBU(k)
    task_t *ben = scheduler_current_task();
    if (!ben) return;
    if (__atomic_load_n(&k->sahip, __ATOMIC_ACQUIRE) == ben) { k->derinlik++; return; }
    mutex_lock(&k->m);
    __atomic_store_n(&k->sahip, ben, __ATOMIC_RELEASE);
    k->derinlik = 1;
    k->alinma++;
}

void ozkilit_birak(ozkilit_t *k) {
    KONTROL_GRUBU(k)
    task_t *ben = scheduler_current_task();
    if (!ben) return;
    if (__atomic_load_n(&k->sahip, __ATOMIC_ACQUIRE) != ben) return;   /* sahibi degil */
    if (--k->derinlik > 0) return;
    __atomic_store_n(&k->sahip, (task_t *)0, __ATOMIC_RELEASE);
    mutex_unlock(&k->m);
}

int ozkilit_bende_mi(ozkilit_t *k) {
    task_t *ben = scheduler_current_task();
    return ben && __atomic_load_n(&k->sahip, __ATOMIC_ACQUIRE) == ben;
}
