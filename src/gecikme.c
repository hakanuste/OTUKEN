#include "gecikme.h"
#include "io.h"
#include "spinlock.h"

/* SMP adim 6: port 0x42/0x43/0x61 PAYLASIMLI. Iki cekirdek ayni anda
 * programlarsa sayim bozulur. Her parca (<= 50 ms) kilit altinda. */
static spinlock_t pit2_kilit;

/* =================== PIT kanal 2 ile gecikme ===================
 * Kesmeler kapaliyken (sti oncesi) zamanlayici tik'i yok; INIT-SIPI
 * zamanlamasi icin PIT kanal 2 yoklanir (hoparlor kapali, yalniz
 * gate). Donanim yanit vermezse sonsuz donguye girmemek icin sinir. */
void pit2_bekle_us(uint32_t us) {
    while (us) {
        uint32_t parca = us > 50000 ? 50000 : us;
        us -= parca;
        uint32_t sayim = (uint32_t)(((uint64_t)parca * 1193182ULL) / 1000000ULL);
        if (sayim < 2) sayim = 2;
        uint64_t kf = spin_lock_irqsave(&pit2_kilit);
        uint8_t p61 = inb(0x61);
        outb(0x61, (uint8_t)(p61 & ~0x03));          /* gate=0, hoparlor=0 */
        outb(0x43, 0xB0);                            /* kanal2, lo/hi, mod0 */
        outb(0x42, (uint8_t)(sayim & 0xFF));
        outb(0x42, (uint8_t)(sayim >> 8));
        outb(0x61, (uint8_t)((p61 & ~0x02) | 0x01)); /* gate=1: say */
        for (uint32_t sinir = 0; sinir < 20000000u; sinir++)
            if (inb(0x61) & 0x20) break;
        outb(0x61, (uint8_t)(p61 & ~0x03));
        spin_unlock_irqrestore(&pit2_kilit, kf);
    }
}

