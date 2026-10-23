#ifndef OTUKENOS_FAT32_H
#define OTUKENOS_FAT32_H
#include <stdint.h>
#define FAT32_MAX_NAME 13
typedef struct {
    char name[FAT32_MAX_NAME];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t is_dir;
} fat32_dirent_t;

/* Ikinci diskteki (disk_id 1) FAT32 birimini baglar. Birim diskin basinda
 * ya da bir GPT/MBR bolumunun icinde olabilir; 2 TB ustu 64-bit LBA'lar
 * desteklenir. 0 = basarili, -1 = FAT32 bulunamadi. */
int fat32_mount(void);
int fat32_list_root(fat32_dirent_t *out, int max);
int fat32_read_file_at(const fat32_dirent_t *entry, uint8_t *buf, uint32_t bufsize, uint32_t offset);

/* Yol tabanli (alt dizinli) islemler. Yollar "/" ile baslar, adlar 8.3'tur
 * (harf duyarsiz). */
int fat32_yol_bul(const char *yol, fat32_dirent_t *out);
int fat32_listele_yol(const char *yol, fat32_dirent_t *out, int max);
/* Dosyayi olusturur ya da tamamen degistirir. Basari: yazilan bayt sayisi,
 * hata: -1 (yarim kalan yazim geri alinir, eski dosya bozulmaz). */
int fat32_write_file(const char *yol, const uint8_t *veri, uint32_t boyut);
/* Dizin olusturur. 0 tamam, -2 zaten var, -1 hata. */
int fat32_dizin_yarat(const char *yol);
#endif
