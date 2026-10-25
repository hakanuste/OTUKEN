#ifndef OTUKENOS_DISK_CACHE_H
#define OTUKENOS_DISK_CACHE_H
#include <stdint.h>
void disk_cache_init(void);
/* disk_id: 0=birincil disk (fat16), 1=ikincil disk (fat32) - AYNI LBA
 * numarasi FARKLI disklerde farkli veri anlamina gelir, bu yuzden
 * onbellek anahtari (disk_id,lba) ikilisi olmalidir. */
/* LBA 64-bit: 2 TB ustu diskler icin SART (uint32_t 2 TB'da tasar). */
int disk_cache_read_sectors(uint8_t disk_id, uint64_t lba, uint8_t count, void *buf);
/* Asama 27: yazma - onbellegi de GUNCELLER (write-through). Onbellek
 * guncellenmezse sonraki okumalar ESKI veriyi dondururdu. */
int disk_cache_write_sectors(uint8_t disk_id, uint64_t lba, uint8_t count, const void *buf);

void disk_cache_stats(uint32_t *hits, uint32_t *misses);
#endif
