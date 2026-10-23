#ifndef OTUKENOS_SPINLOCK_H
#define OTUKENOS_SPINLOCK_H

#include <stdint.h>

/* --- SMP'ye hazir spinlock ---
 *
 * ONCEKI TASARIMIN KUSURU: spinlock_release() KOSULSUZ `sti` yapiyordu.
 * Bu, kilit alinmadan once kesmeler ZATEN KAPALIYSA (ic ice kilit, ya da
 * kesme isleyicisi icinden cagri) onlari ERKEN aciyordu - kritik bolge
 * yarida kesilebiliyordu.
 *
 * Dogru desen, kesme bayragini KAYDEDIP geri yuklemektir. Bayragi kilidin
 * kendi icinde tutuyoruz; kilit tutulurken sahibi TEK oldugu icin bu
 * guvenlidir ve cagiranin ekstra degisken tasimasini gerektirmez.
 *
 * SMP NOTU: `cli` yalnizca KENDI cekirdeginin kesmelerini kapatir. Baska
 * bir cekirdege karsi korumayi saglayan sey `atomic_test_and_set`
 * (lock xchg) uzerine kurulu donme dongusudur. Ikisi birlikte gerekir:
 *   - cli : ayni cekirdekte kesme isleyicisiyle kilitlenmeyi onler
 *   - atomik: diger cekirdeklerle yarisi onler
 */

typedef struct {
    volatile uint32_t locked;
    uint64_t saved_flags;      /* kilit tutulurken gecerli */
} spinlock_t;

void spinlock_init(spinlock_t *lock);

/* Kesmeleri kaydedip kapatir, sonra kilidi alir. */
void spinlock_acquire(spinlock_t *lock);

/* Kilidi birakir ve kesme bayragini ONCEKI haline dondurur. */
void spinlock_release(spinlock_t *lock);

/* Cagiranin bayragi kendi tasidigi surum (ic ice kullanim icin). */
uint64_t spin_lock_irqsave(spinlock_t *lock);
void     spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags);

/* Kilitsiz, yalnizca kesme koruması gereken KISA bolgeler icin.
 * SMP'de TEK BASINA YETERLI DEGILDIR - paylasilan veriye erisiyorsan
 * mutlaka bir spinlock da al. */
static inline uint64_t irq_kaydet_kapat(void) {
    uint64_t f;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) : : "memory");
    return f;
}
static inline void irq_geri_yukle(uint64_t f) {
    __asm__ volatile ("push %0; popfq" : : "r"(f) : "memory", "cc");
}

/* --- OZYINELEMELI DONEN KILIT (SMP adim 6) ---
 * Cekirdek-sahipli: ayni CEKIRDEK ic ice alabilir (orn. log_write ->
 * log_write_num, ya da istisna isleyicisi ayni kilidi isterse). Kisa
 * islemler icindir; kesmeler kapali tutulur. Beklerken bekleyen IPI'ler
 * (TLB vurusu) islenir. Sifirla baslatilmis statik degisken hazirdir
 * ANCAK sahip alani -1 olmalidir: bu yuzden OZSPIN_BASLANGIC kullanin. */
typedef struct {
    volatile int kilit;
    volatile int sahip;       /* cekirdek indeksi, -1 = bos */
    int          derinlik;
    uint64_t     bayrak;      /* ilk alistaki RFLAGS */
} ozspin_t;
#define OZSPIN_BASLANGIC { 0, -1, 0, 0 }

void ozspin_al(ozspin_t *k);
void ozspin_birak(ozspin_t *k);

#endif
