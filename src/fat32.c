#include "fat32.h"
#include "disk.h"
#include "disk_cache.h"
#include "serial.h"

static int fat32_bagli = 0;
static uint16_t bytes_per_sector;
static uint8_t  sectors_per_cluster;
static uint16_t reserved_sectors;
static uint8_t  num_fats;
static uint32_t fat_size_32;
static uint32_t root_cluster;
static uint32_t fat_start_lba;
static uint32_t data_start_lba;

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fat32_mount(void) {
    static uint8_t sector[512];
    if (disk2_read_sectors(0, 1, sector) != 0) {
        serial_write("BITIG(fat32): boot sector okunamadi\n");
        return -1;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        serial_write("BITIG(fat32): gecerli onyukleme imzasi yok\n");
        return -1;
    }
    bytes_per_sector    = rd_u16(&sector[11]);
    sectors_per_cluster = sector[13];
    reserved_sectors    = rd_u16(&sector[14]);
    num_fats            = sector[16];
    fat_size_32         = rd_u32(&sector[36]);
    root_cluster        = rd_u32(&sector[44]);
    if (bytes_per_sector != 512) {
        serial_write("BITIG(fat32): UYARI - 512 disi sektor boyutu desteklenmiyor\n");
        return -1;
    }
    fat_start_lba  = reserved_sectors;
    data_start_lba = fat_start_lba + (uint32_t)num_fats * fat_size_32;
    fat32_bagli = 1;
    serial_write("BITIG(fat32): baglandi - sektor/kume=");
    serial_write_hex(sectors_per_cluster);
    serial_write(" fat_lba="); serial_write_hex(fat_start_lba);
    serial_write(" data_lba="); serial_write_hex(data_start_lba);
    serial_write(" kok_kume="); serial_write_hex(root_cluster);
    serial_write("\n");
    return 0;
}

static uint32_t fat32_next_cluster(uint32_t cluster) {
    static uint8_t fat_sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sec = fat_start_lba + (fat_offset / bytes_per_sector);
    uint32_t ent_off = fat_offset % bytes_per_sector;
    if (disk_cache_read_sectors(1, fat_sec, 1, fat_sector) != 0) return 0x0FFFFFFF;
    return rd_u32(&fat_sector[ent_off]) & 0x0FFFFFFF;
}

static void format_83_name(const uint8_t *raw11, char *out) {
    int p = 0;
    for (int i = 0; i < 8; i++) { if (raw11[i] == ' ') break; out[p++] = (char)raw11[i]; }
    if (raw11[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11; i++) { if (raw11[i] == ' ') break; out[p++] = (char)raw11[i]; }
    }
    out[p] = 0;
}

int fat32_list_root(fat32_dirent_t *out, int max) {
    /* DAYANIKLILIK: birim bagli degilse sifira bolme olurdu. */
    if (!fat32_bagli || bytes_per_sector == 0 || sectors_per_cluster == 0) return 0;

    static uint8_t sector[512];
    int found = 0;
    uint32_t cluster = root_cluster;
    while (cluster < 0x0FFFFFF8 && cluster != 0 && found < max) {
        uint32_t lba = data_start_lba + (cluster - 2) * sectors_per_cluster;
        for (int s = 0; s < sectors_per_cluster && found < max; s++) {
            if (disk_cache_read_sectors(1, lba + s, 1, sector) != 0) {
                serial_write("BITIG(fat32): kok dizin sektoru okunamadi\n");
                return found;
            }
            for (int e = 0; e < 16 && found < max; e++) {
                const uint8_t *ent = &sector[e * 32];
                uint8_t first = ent[0];
                if (first == 0x00) return found;
                if (first == 0xE5) continue;
                uint8_t attr = ent[11];
                if (attr == 0x0F) continue;
                if (attr & 0x08) continue;
                format_83_name(ent, out[found].name);
                uint16_t clus_hi = rd_u16(&ent[20]);
                uint16_t clus_lo = rd_u16(&ent[26]);
                out[found].first_cluster = ((uint32_t)clus_hi << 16) | clus_lo;
                out[found].size = rd_u32(&ent[28]);
                out[found].is_dir = (attr & 0x10) ? 1 : 0;
                found++;
            }
        }
        cluster = fat32_next_cluster(cluster);
    }
    return found;
}

int fat32_read_file_at(const fat32_dirent_t *entry, uint8_t *buf, uint32_t bufsize, uint32_t offset) {
    /* DAYANIKLILIK: birim bagli degilse sifira bolme olurdu. */
    if (!fat32_bagli || bytes_per_sector == 0 || sectors_per_cluster == 0) return -1;

    static uint8_t cluster_buf[512];
    if (offset >= entry->size || entry->first_cluster == 0) return 0;
    uint32_t cluster_size_bytes = (uint32_t)bytes_per_sector * sectors_per_cluster;
    uint32_t clusters_to_skip = offset / cluster_size_bytes;
    uint32_t byte_offset_in_cluster = offset % cluster_size_bytes;
    uint32_t cluster = entry->first_cluster;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        cluster = fat32_next_cluster(cluster);
        if (cluster == 0 || cluster >= 0x0FFFFFF8) return 0;
    }
    uint32_t remaining = entry->size - offset;
    uint32_t total_read = 0;
    int first_iter = 1;
    while (cluster < 0x0FFFFFF8 && cluster != 0 && remaining > 0 && total_read < bufsize) {
        uint32_t lba = data_start_lba + (cluster - 2) * sectors_per_cluster;
        uint32_t start_sector = first_iter ? (byte_offset_in_cluster / bytes_per_sector) : 0;
        uint32_t start_byte = first_iter ? (byte_offset_in_cluster % bytes_per_sector) : 0;
        for (uint32_t s = start_sector; s < (uint32_t)sectors_per_cluster && remaining > 0 && total_read < bufsize; s++) {
            if (disk_cache_read_sectors(1, lba + s, 1, cluster_buf) != 0) return (int)total_read;
            uint32_t sb = (s == start_sector) ? start_byte : 0;
            for (uint32_t i = sb; i < bytes_per_sector && remaining > 0 && total_read < bufsize; i++) {
                buf[total_read++] = cluster_buf[i];
                remaining--;
            }
        }
        first_iter = 0;
        cluster = fat32_next_cluster(cluster);
    }
    return (int)total_read;
}
