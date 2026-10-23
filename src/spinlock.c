#include "spinlock.h"
#include "atomic.h"
#include "smp.h"
#include "cpu.h"

void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
    lock->saved_flags = 0;
}

uint64_t spin_lock_irqsave(spinlock_t *lock) {
    uint64_t flags = irq_kaydet_kapat();
    /* Diger CEKIRDEKLERE karsi koruma burada: atomik test-and-set.
     * Tek cekirdekte bu dongu genelde ilk denemede gecer. */
    while (atomic_test_and_set(&lock->locked)) {
        /* SMP: kilit sahibi bizden TLB vurusu bekliyor olabilir, biz
         * ise IF=0 ile donuyoruz -> bekleyen cagriyi burada isle. */
        smp_bekleyen_isle();
        __asm__ volatile ("pause");
    }
    return flags;
}

void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    atomic_clear(&lock->locked);
    irq_geri_yukle(flags);
}

void spinlock_acquire(spinlock_t *lock) {
    uint64_t flags = spin_lock_irqsave(lock);
    lock->saved_flags = flags;     /* kilit bizde: yazmak guvenli */
}

void spinlock_release(spinlock_t *lock) {
    uint64_t flags = lock->saved_flags;
    spin_unlock_irqrestore(lock, flags);
}

void ozspin_al(ozspin_t *k) {
    uint64_t f = irq_kaydet_kapat();
    int ben = (int)cpu_bu()->indeks;
    if (__atomic_load_n(&k->sahip, __ATOMIC_ACQUIRE) == ben) { k->derinlik++; return; }
    while (__atomic_exchange_n(&k->kilit, 1, __ATOMIC_ACQUIRE)) {
        smp_bekleyen_isle();
        __asm__ volatile ("pause");
    }
    __atomic_store_n(&k->sahip, ben, __ATOMIC_RELEASE);
    k->derinlik = 1;
    k->bayrak = f;
}

void ozspin_birak(ozspin_t *k) {
    if (--k->derinlik > 0) return;      /* ic ice: bayrak disarida geri yuklenir */
    uint64_t f = k->bayrak;
    __atomic_store_n(&k->sahip, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&k->kilit, 0, __ATOMIC_RELEASE);
    irq_geri_yukle(f);
}
