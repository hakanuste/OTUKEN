/* FAT16 surucusunun IC arayuzu: alt dizin katmani (fat16_dizin.c)
 * bu islevlerle surucunun durumuna erisir. Disariya acik degildir. */
#ifndef OTUKENOS_FAT16_IC_H
#define OTUKENOS_FAT16_IC_H

#include <stdint.h>
#include "fat16.h"

int      fat16_bagli_mi(void);
/* 1.4.9: birim geometrisi (ASENA onarımı, ERGENEKON biçimleme) */
typedef struct {
    uint32_t bayt_sektor, sektor_kume, fat_sayisi, fat_lba, fat_sektor;
    uint32_t kok_lba, kok_sektor, kok_giris, veri_lba, toplam_sektor, kume_sayisi;
} fat16_geo_t;
int      fat16_geometri(fat16_geo_t *g);
void     fat16_ipucu_sifirla(void);
extern volatile int fat16_sinama_yazim_hatasi;   /* yalnız çekirdek içi sınama */
uint32_t fat16_root_lba(void);
uint32_t fat16_root_dir_sectors(void);
uint32_t fat16_sectors_per_cluster(void);
uint32_t fat16_kume_lba(uint16_t kume);
int      fat16_kume_gecerli(uint16_t kume);   /* birimin veri bölgesinde mi */
uint16_t fat16_sonraki_kume(uint16_t kume);
uint16_t fat16_bos_kume_bul(void);
int      fat16_kume_yaz(uint16_t kume, uint16_t deger);
void     fat16_ad_bicimle(const uint8_t *ham11, char *cikti);
void     fat16_ad_83_yap(const char *ad, uint8_t *cikti11);
uint16_t fat16_oku16(const uint8_t *p);
uint32_t fat16_oku32(const uint8_t *p);
void     fat16_yaz16(uint8_t *p, uint16_t v);
void     fat16_yaz32(uint8_t *p, uint32_t v);

/* Alt dizin katmani */
int fat16_yol_bul(const char *yol, fat16_dirent_t *cikti);
int fat16_listele_yol(const char *yol, fat16_dirent_t *out, int max);
int fat16_dizin_yarat(const char *yol);
/* 1.4.9: alt dizinde yaz (baştan), sonuna ekle, sil (dizin yalnız boşsa: -2).
 * yaz/ekle: yazılan bayt / -1 */
int fat16_yol_yaz(const char *yol, const uint8_t *veri, uint32_t boy);
int fat16_yol_ekle(const char *yol, const uint8_t *veri, uint32_t boy);
int fat16_yol_sil(const char *yol);
#endif
