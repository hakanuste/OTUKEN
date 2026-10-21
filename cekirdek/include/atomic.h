#ifndef OTUKENOS_ATOMIC_H
#define OTUKENOS_ATOMIC_H

#include <stdint.h>

/* x86-64'te bu GCC __sync builtin'leri, LOCK onekli gercek atomik CPU
 * komutlarina (lock xadd, lock cmpxchg vb.) karsilik gelir - hem SMP
 * hem de kesmelere karsi guvenlidir (bir kesme bu komutlarin ORTASINDA
 * asla devreye giremez, x86 komutlari atomiktir). */

static inline uint64_t atomic_add(volatile uint64_t *v, uint64_t delta) {
    return __sync_fetch_and_add(v, delta); /* ESKI degeri dondurur */
}

static inline uint64_t atomic_inc(volatile uint64_t *v) {
    return __sync_add_and_fetch(v, 1); /* YENI degeri dondurur */
}

static inline uint64_t atomic_dec(volatile uint64_t *v) {
    return __sync_sub_and_fetch(v, 1);
}

/* Beklenen deger v'deki ile ayniysa yenisiyle degistirir.
 * Basari: 1, basarisiz (v beklenenden farkliydi): 0. */
static inline int atomic_cmpxchg(volatile uint64_t *v, uint64_t expected, uint64_t new_val) {
    return __sync_bool_compare_and_swap(v, expected, new_val);
}

/* Spinlock icin: onceki degeri dondurup 1 yazar (test-and-set). */
static inline uint32_t atomic_test_and_set(volatile uint32_t *v) {
    return __sync_lock_test_and_set(v, 1);
}

static inline void atomic_clear(volatile uint32_t *v) {
    __sync_lock_release(v);
}

#endif
