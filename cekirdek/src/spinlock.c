#include "spinlock.h"
#include "atomic.h"

void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
    lock->saved_flags = 0;
}

uint64_t spin_lock_irqsave(spinlock_t *lock) {
    uint64_t flags = irq_kaydet_kapat();
    /* Diger CEKIRDEKLERE karsi koruma burada: atomik test-and-set.
     * Tek cekirdekte bu dongu genelde ilk denemede gecer. */
    while (atomic_test_and_set(&lock->locked)) {
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
