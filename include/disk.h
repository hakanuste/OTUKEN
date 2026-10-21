#ifndef OTUKENOS_DISK_H
#define OTUKENOS_DISK_H

#include <stdint.h>

/* --- BUYUK DISK DESTEGI (LBA48) ---
 * Onceden LBA28 kullaniliyordu: sektor adresi 28 bit ile sinirliydi,
 * yani en fazla 2^28 * 512 = 128 GB. 2/4/8 TB SSD'lerde diskin ancak
 * ilk 128 GB'i gorulebiliyordu.
 *
 * Artik ATA LBA48 komutlari kullaniliyor (READ/WRITE SECTORS EXT).
 * Adres 48 bit -> 2^48 * 512 = 128 PB tavan. LBA tipleri de uint64_t
 * yapildi; uint32_t 2 TB'da TASIYORDU. */

int disk_init(void);

/* count adet 512-baytlik sektoru lba'dan itibaren okur.
 * count 0 ise 256 sektor anlamina gelmez; 1..255 kullanin.
 * Basari: 0, hata: -1 */
int disk_read_sectors(uint64_t lba, uint8_t count, void *buf);
int disk_write_sectors(uint64_t lba, uint8_t count, const void *buf);

/* Ikinci disk (birincil ATA kanali SLAVE surucusu). */
int disk2_init(void);
int disk2_read_sectors(uint64_t lba, uint8_t count, void *buf);

/* IDENTIFY ile bildirilen toplam sektor sayisi (0 = bilinmiyor).
 * 8 TB disk icin ~15.6 milyar sektor - 64-bit SART. */
uint64_t disk_sektor_sayisi(int disk_id);

/* Surucu LBA48 destekliyor mu (IDENTIFY'dan). */
int disk_lba48_var(int disk_id);

#endif
