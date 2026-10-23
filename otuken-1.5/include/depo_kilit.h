#ifndef OTUKENOS_DEPO_KILIT_H
#define OTUKENOS_DEPO_KILIT_H
#include "mutex.h"

/* --- SMP adim 6: DEPOLAMA YIGINI KILIDI ---
 * disk (ATA PIO), ahci, disk_cache, fat16 (+ alt dizin), fat32, vfs ve
 * gpt TEK bir ozyinelemeli gorev kilidini paylasir. Katmanlar ic ice
 * cagrildigi (VFS -> FAT16 -> onbellek -> disk) ve ortak durum tasidigi
 * (FAT tablosu, onbellek, ATA yazmaclari, acik dosya tablosu) icin tek
 * kilit hem dogru hem kilitlenmesizdir. Uyuyan kilittir: ATA beklemeleri
 * saniyeler surebilir; spinlock tutulamaz.
 * SIRA KURALI: gorsel_kilit -> depo_kilit (dosya yoneticisi penceresi).
 * Depolama kodu gorsel/ag kilidi ALMAZ. */
extern ozkilit_t depo_kilit;

#endif
