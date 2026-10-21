#include "vfs_fat16.h"
#include "fat16.h"
#include "fat16_ic.h"
#include "serial.h"

#define ADAPTER_MAX_FILES 16

/* FAT dosya adlari TASARIM GEREGI buyuk/kucuk harf DUYARSIZDIR.
 * Duyarli karsilastirma, kullanicinin "notlar.txt" yazip dosyayi
 * bulamamasina yol aciyordu (gercek kullanim testinde bulundu). */
static char buyuk_harf(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int str_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (buyuk_harf(a[i]) != buyuk_harf(b[i])) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int fat16_vfs_list_dir(const char *path, vfs_dirent_t *out, int max, void *ctx) {
    (void)ctx;
    /* Asama 42: alt dizinler destekleniyor. Kok icin eski yol,
     * digerleri icin yol cozumlemeli listeleme kullanilir. */
    fat16_dirent_t tmp[ADAPTER_MAX_FILES];
    int cap = (max < ADAPTER_MAX_FILES) ? max : ADAPTER_MAX_FILES;
    int alt = !(path[0] == '/' && path[1] == 0);
    int n = alt ? fat16_listele_yol(path, tmp, cap)
                : fat16_list_root(tmp, cap);
    if (n < 0) return -1;

    for (int i = 0; i < n; i++) {
        int j = 0;
        while (tmp[i].name[j] && j < VFS_MAX_NAME - 1) {
            out[i].name[j] = tmp[i].name[j];
            j++;
        }
        out[i].name[j] = 0;
        out[i].size = tmp[i].size;
        out[i].is_dir = tmp[i].is_dir;
    }
    return n;
}

/* FAT16'nin kendi API'si "yola gore ac" degil "dizin girisine gore
 * oku" seklinde oldugundan, burada once kok dizini tarayip ismi
 * eslesen girisi buluyoruz. */
/* Asama 42: yol cozumlemesi. Once ALT DIZIN katmani denenir; o
 * basarisiz olursa eski kok-dizin aramasina dusulur (uyumluluk). */
static int find_dirent_yol(const char *tam_yol, fat16_dirent_t *out) {
    if (fat16_yol_bul(tam_yol, out) == 0) return 0;
    return -1;
}

static int find_dirent(const char *filename, fat16_dirent_t *out) {
    fat16_dirent_t tmp[ADAPTER_MAX_FILES];
    int n = fat16_list_root(tmp, ADAPTER_MAX_FILES);
    for (int i = 0; i < n; i++) {
        if (str_eq(tmp[i].name, filename)) {
            *out = tmp[i];
            return 0;
        }
    }
    return -1;
}

static int fat16_vfs_read_file(const char *path, uint8_t *buf, uint64_t bufsize,
                                uint64_t offset, uint64_t *out_filesize, void *ctx) {
    (void)ctx;
    if (path[0] != '/') return -1;
    const char *filename = path + 1;

    fat16_dirent_t entry;
    if (find_dirent_yol(path, &entry) != 0 &&
        find_dirent(filename, &entry) != 0) return -1;

    if (out_filesize) *out_filesize = entry.size;
    if (bufsize == 0) return 0;

    int n = fat16_read_file_at(&entry, buf, (uint32_t)bufsize, (uint32_t)offset);
    if (n < 0) return -1;
    return n;
}

/* FAT16 yalniz KOK dizine dosya yazar ("/AD.UZN", 8.3). Alt dizin yolu
 * acikca reddedilir; dizin olusturma alt dizin katmanina gider. */
static int fat16_vfs_write_file(const char *path, const uint8_t *data, uint64_t size, void *ctx) {
    (void)ctx;
    if (path[0] != '/' || path[1] == 0 || size > 0xFFFFFFFFULL) return -1;
    for (int i = 1; path[i]; i++) if (path[i] == '/') return -1;
    return fat16_write_file(path + 1, data, (uint32_t)size);
}

static int fat16_vfs_mkdir(const char *path, void *ctx) {
    (void)ctx;
    return fat16_dizin_yarat(path);
}

static const vfs_ops_t fat16_vfs_ops = {
    .list_dir = fat16_vfs_list_dir,
    .read_file = fat16_vfs_read_file,
    .write_file = fat16_vfs_write_file,
    .mkdir = fat16_vfs_mkdir,
};

int vfs_fat16_mount_root(void) {
    int r = vfs_mount("/", &fat16_vfs_ops, 0);
    if (r == 0) {
        serial_write("vfs-fat16: FAT16 '/' noktasina baglandi\n");
    }
    return r;
}
