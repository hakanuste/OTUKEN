#include "disk_cache.h"
#include "depo_kilit.h"
#include "disk.h"
#define CACHE_ENTRIES 48
typedef struct { int valid; uint8_t disk_id; uint64_t lba; uint8_t data[512]; } cache_entry_t;
static cache_entry_t cache[CACHE_ENTRIES];
static uint32_t next_evict = 0, hit_count = 0, miss_count = 0;
void disk_cache_init(void) {
    for (int i = 0; i < CACHE_ENTRIES; i++) cache[i].valid = 0;
    next_evict = 0; hit_count = 0; miss_count = 0;
}
static int find_cache(uint8_t disk_id, uint64_t lba) {
    for (int i = 0; i < CACHE_ENTRIES; i++)
        if (cache[i].valid && cache[i].disk_id == disk_id && cache[i].lba == lba) return i;
    return -1;
}
static int disk_cache_read_sectors__ic(uint8_t disk_id, uint64_t lba, uint8_t count, void *buf) {
    uint8_t *out = (uint8_t *)buf;
    for (uint8_t s = 0; s < count; s++) {
        uint64_t this_lba = lba + s;
        int idx = find_cache(disk_id, this_lba);
        if (idx >= 0) {
            hit_count++;
            for (int i = 0; i < 512; i++) out[(uint32_t)s*512+i] = cache[idx].data[i];
        } else {
            miss_count++;
            uint8_t sector_buf[512];
            int r = (disk_id == 0) ? disk_read_sectors(this_lba, 1, sector_buf)
                                     : disk2_read_sectors(this_lba, 1, sector_buf);
            if (r != 0) return -1;
            for (int i = 0; i < 512; i++) out[(uint32_t)s*512+i] = sector_buf[i];
            int evict = (int)next_evict;
            next_evict = (next_evict + 1) % CACHE_ENTRIES;
            cache[evict].valid = 1;
            cache[evict].disk_id = disk_id;
            cache[evict].lba = this_lba;
            for (int i = 0; i < 512; i++) cache[evict].data[i] = sector_buf[i];
        }
    }
    return 0;
}
void disk_cache_stats(uint32_t *hits, uint32_t *misses) {
    if (hits) *hits = hit_count;
    if (misses) *misses = miss_count;
}


static int disk_cache_write_sectors__ic(uint8_t disk_id, uint64_t lba, uint8_t count, const void *buf) {
    const uint8_t *in = (const uint8_t *)buf;
    if (disk_id > 1) return -1;

    if (disk_id == 0) {
        if (disk_write_sectors(lba, count, buf) != 0) return -1;
    } else {
        if (disk2_write_sectors(lba, count, buf) != 0) return -1;
    }

    /* write-through: onbellekteki kopyalari da tazele */
    for (uint8_t s = 0; s < count; s++) {
        uint64_t this_lba = lba + s;
        int idx = find_cache(disk_id, this_lba);
        if (idx >= 0) {
            for (int i = 0; i < 512; i++)
                cache[idx].data[i] = in[(uint32_t)s * 512 + i];
        }
    }
    return 0;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int disk_cache_read_sectors(uint8_t disk_id, uint64_t lba, uint8_t count, void *buf) {
    ozkilit_al(&depo_kilit);
    int r = disk_cache_read_sectors__ic(disk_id, lba, count, buf);
    ozkilit_birak(&depo_kilit);
    return r;
}

int disk_cache_write_sectors(uint8_t disk_id, uint64_t lba, uint8_t count, const void *buf) {
    ozkilit_al(&depo_kilit);
    int r = disk_cache_write_sectors__ic(disk_id, lba, count, buf);
    ozkilit_birak(&depo_kilit);
    return r;
}
