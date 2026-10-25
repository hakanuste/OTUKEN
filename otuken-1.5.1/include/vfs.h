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
    /* Asagidakiler ISTEGE BAGLIDIR (yoksa 0): salt-okunur ya da eski
     * dosya sistemleri bunlari bos birakir. */
    /* Dosyayi olusturur/degistirir. Basari: yazilan bayt, hata: <0. */
    int (*write_file)(const char *path, const uint8_t *data, uint64_t size, void *fs_ctx);
    /* Dizin olusturur: 0 tamam, -2 zaten var, diger <0 hata. */
    int (*mkdir)(const char *path, void *fs_ctx);
    /* Dosya bilgisi: 0 bulundu, -1 yok. */
    int (*stat)(const char *path, uint64_t *size, int *is_dir, void *fs_ctx);
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
/* Yola dosya yazar (var olani degistirir). Yol, ilgili baglama noktasina
 * yonlendirilir. Basari: yazilan bayt sayisi, hata: -1. */
int vfs_write_file(const char *path, const void *data, uint64_t size);

/* Basarili: bir dosya tanitici (fd, >=0) doner. Hata: -1. */
int vfs_open(const char *path);
int vfs_read(int fd, void *buf, uint64_t size); /* okunan bayt sayisini dondurur */
int vfs_close(int fd);
uint64_t vfs_lseek(int fd, uint64_t new_offset);

/* Denetim eklemesi: acik dosya tanimlayicilari artik acan goreve aittir.
 * vfs_fd_gorevin_mi: fd acik VE gorev_id tarafindan acilmis ise 1.
 * vfs_gorev_dosyalarini_kapat: sonlanan gorevin unutulan fd'lerini iade
 * eder (yoksa tablo zamanla dolar ve baskalari dosya acamaz). */
int  vfs_fd_gorevin_mi(int fd, uint32_t gorev_id);
void vfs_gorev_dosyalarini_kapat(uint32_t gorev_id);

#endif
