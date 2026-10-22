#ifndef OTUKENOS_FAT16_H
#define OTUKENOS_FAT16_H

#include <stdint.h>

#define FAT16_MAX_NAME 13

typedef struct {
    char     name[FAT16_MAX_NAME]; /* "AD.UZANTI" bicimi, sonu NUL */
    uint32_t size;
    uint16_t first_cluster;
    uint8_t  is_dir;
} fat16_dirent_t;

/* Diskteki FAT16 birimini "bagla" (boot sector'u okuyup gerekli
 * alan konumlarini hesaplar). Basari: 0, hata: -1. */
int fat16_mount(void);

/* Kok dizindeki dosyalari out dizisine yazar, en fazla max adet.
 * Donus: bulunan dosya sayisi. */
int fat16_list_root(fat16_dirent_t *out, int max);

/* entry'nin isaret ettigi dosyayi buf'a okur (en fazla bufsize bayt).
 * Donus: okunan gercek bayt sayisi, hata: -1. */
int fat16_read_file(const fat16_dirent_t *entry, uint8_t *buf, uint32_t bufsize);
int fat16_read_file_at(const fat16_dirent_t *entry, uint8_t *buf, uint32_t bufsize, uint32_t offset);

/* --- Asama 27: FAT16 yazma ---
 * Var olan dosyayi BASTAN yazar (uzunlugu gunceller); dosya yoksa
 * kok dizinde yeni girdi olusturur.
 * Basari: yazilan bayt sayisi, hata: -1
 * NOT: bu prototip eksikti - prototipsiz cagri, derleyicinin donus ve
 * arguman tiplerini YANLIS varsaymasina yol acar (64-bit isaretciler
 * icin gercek bir risk). Denetimde yakalandi. */
int fat16_write_file(const char *name83, const uint8_t *data, uint32_t size);
/* 1.4.9: kok dizinden dosya sil (0 / -1) ve bos kume sayisi */
int fat16_sil(const char *name83);
uint32_t fat16_bos_kume_sayisi(void);

#endif
