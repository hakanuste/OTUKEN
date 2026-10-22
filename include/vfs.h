#ifndef OTUKENOS_VFS_H
#define OTUKENOS_VFS_H

#include <stdint.h>

#define VFS_MAX_MOUNTS      4
#define VFS_MAX_OPEN_FILES 16
#define VFS_MAX_PATH      256
#define VFS_MAX_NAME       64

typedef struct {
    char name[VFS_MAX_NAME];
    uint64_t size;
    int is_dir;
} vfs_dirent_t;

/* Her dosya sistemi turunun uygulamasi gereken islemler (vtable).
 * 'path', ILGILI MOUNT NOKTASINA GORE (bagimsiz) yoldur - VFS
 * katmani bunu cozup buraya kadar getirir. */
typedef struct {
    int (*list_dir)(const char *path, vfs_dirent_t *out, int max, void *fs_ctx);
    int (*read_file)(const char *path, uint8_t *buf, uint64_t bufsize,
                      uint64_t offset, uint64_t *out_filesize, void *fs_ctx);
} vfs_ops_t;

void vfs_init(void);

/* Bir dosya sistemini belirtilen noktaya baglar. Basari:0, hata:-1
 * (tablo dolu). mount_point su an sadece "/" olarak destekleniyor -
 * ic ice/coklu mount noktalari gelecekte genisletilebilir. */
int vfs_mount(const char *mount_point, const vfs_ops_t *ops, void *fs_ctx);

int vfs_list_dir(const char *path, vfs_dirent_t *out, int max);
/* Dosya bilgisi: 0 bulundu, -1 yok. Harf duyarsiz. */
int vfs_stat(const char *path, uint64_t *boyut, int *dizin_mi);
/* Dizin olusturur. 0 tamam, -2 zaten var, digerleri surucu hatasi. */
int vfs_mkdir(const char *path);

/* Basarili: bir dosya taniticisi (fd, >= 16) doner. Hata: -1.
 * 1.4.7: tanitici = (kusak << 4) | yuva; yalniz ACAN gorev kullanabilir.
 * Kapatilmis ya da yuvasi yeniden kullanilmis (BAYAT) tanitici, baska
 * gorevin taniticisi ve gecersiz deger -1 doner (read/close/lseek). */
int vfs_open(const char *path);
int vfs_read(int fd, void *buf, uint64_t size); /* okunan bayt sayisini dondurur */
int vfs_close(int fd);
uint64_t vfs_lseek(int fd, uint64_t new_offset);

/* Reap'ten: olen gorevin acik biraktigi dosyalari kapatir (kapatilan sayi).
 * Yalniz donen kilit alir; uyumaz. */
int vfs_gorev_bitti(uint32_t gorev_id);
/* Sinama: sistemde acik tanitici sayisi. */
int vfs_acik_sayisi(void);

#endif
