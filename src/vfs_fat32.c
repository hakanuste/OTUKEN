#include "fat32.h"
#include "vfs_fat32.h"
#include "vfs.h"
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
static int fat32_vfs_list_dir(const char *path, vfs_dirent_t *out, int max, void *ctx) {
    (void)ctx;
    if (!(path[0] == '/' && path[1] == 0)) return -1;
    fat32_dirent_t tmp[ADAPTER_MAX_FILES];
    int cap = (max < ADAPTER_MAX_FILES) ? max : ADAPTER_MAX_FILES;
    int n = fat32_list_root(tmp, cap);
    for (int i = 0; i < n; i++) {
        int j = 0;
        while (tmp[i].name[j] && j < VFS_MAX_NAME - 1) { out[i].name[j] = tmp[i].name[j]; j++; }
        out[i].name[j] = 0;
        out[i].size = tmp[i].size;
        out[i].is_dir = tmp[i].is_dir;
    }
    return n;
}
static int find_dirent32(const char *filename, fat32_dirent_t *out) {
    fat32_dirent_t tmp[ADAPTER_MAX_FILES];
    int n = fat32_list_root(tmp, ADAPTER_MAX_FILES);
    for (int i = 0; i < n; i++) if (str_eq(tmp[i].name, filename)) { *out = tmp[i]; return 0; }
    return -1;
}
static int fat32_vfs_read_file(const char *path, uint8_t *buf, uint64_t bufsize, uint64_t offset, uint64_t *out_filesize, void *ctx) {
    (void)ctx;
    if (path[0] != '/') return -1;
    fat32_dirent_t entry;
    if (find_dirent32(path + 1, &entry) != 0) return -1;
    if (out_filesize) *out_filesize = entry.size;
    if (bufsize == 0) return 0;
    int n = fat32_read_file_at(&entry, buf, (uint32_t)bufsize, (uint32_t)offset);
    if (n < 0) return -1;
    return n;
}
static const vfs_ops_t fat32_vfs_ops = { .list_dir = fat32_vfs_list_dir, .read_file = fat32_vfs_read_file };
int vfs_fat32_mount(const char *mount_point) {
    int r = vfs_mount(mount_point, &fat32_vfs_ops, 0);
    if (r == 0) { serial_write("vfs-fat32: FAT32 '"); serial_write(mount_point); serial_write("' noktasina baglandi\n"); }
    return r;
}
