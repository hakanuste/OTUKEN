#include "fat32.h"
#include "vfs.h"
#include "serial.h"
#define ADAPTER_MAX_FILES 64

/* Alt dizinli FAT32 adaptoru: yollar baglama noktasina gore ("/A/B.TXT")
 * dogrudan fat32_* yol islevlerine gider. FAT adlari harf duyarsizdir. */
static int fat32_vfs_list_dir(const char *path, vfs_dirent_t *out, int max, void *ctx) {
    (void)ctx;
    static fat32_dirent_t tmp[ADAPTER_MAX_FILES];
    int cap = (max < ADAPTER_MAX_FILES) ? max : ADAPTER_MAX_FILES;
    int n = fat32_listele_yol(path, tmp, cap);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        int j = 0;
        while (tmp[i].name[j] && j < VFS_MAX_NAME - 1) { out[i].name[j] = tmp[i].name[j]; j++; }
        out[i].name[j] = 0;
        out[i].size = tmp[i].size;
        out[i].is_dir = tmp[i].is_dir;
    }
    return n;
}

static int fat32_vfs_read_file(const char *path, uint8_t *buf, uint64_t bufsize, uint64_t offset, uint64_t *out_filesize, void *ctx) {
    (void)ctx;
    fat32_dirent_t entry;
    if (fat32_yol_bul(path, &entry) != 0 || entry.is_dir) return -1;
    if (out_filesize) *out_filesize = entry.size;
    if (bufsize == 0) return 0;
    if (offset > 0xFFFFFFFFULL) return 0;
    if (bufsize > 0xFFFFFFFFULL) bufsize = 0xFFFFFFFFULL;
    int n = fat32_read_file_at(&entry, buf, (uint32_t)bufsize, (uint32_t)offset);
    if (n < 0) return -1;
    return n;
}

static int fat32_vfs_write_file(const char *path, const uint8_t *data, uint64_t size, void *ctx) {
    (void)ctx;
    if (size > 0xFFFFFFFFULL) return -1;
    return fat32_write_file(path, data, (uint32_t)size);
}

static int fat32_vfs_mkdir(const char *path, void *ctx) {
    (void)ctx;
    return fat32_dizin_yarat(path);
}

static int fat32_vfs_stat(const char *path, uint64_t *size, int *is_dir, void *ctx) {
    (void)ctx;
    fat32_dirent_t d;
    if (fat32_yol_bul(path, &d) != 0) return -1;
    if (size) *size = d.size;
    if (is_dir) *is_dir = d.is_dir;
    return 0;
}

static const vfs_ops_t fat32_vfs_ops = {
    .list_dir = fat32_vfs_list_dir,
    .read_file = fat32_vfs_read_file,
    .write_file = fat32_vfs_write_file,
    .mkdir = fat32_vfs_mkdir,
    .stat = fat32_vfs_stat,
};

int vfs_fat32_mount(const char *mount_point) {
    int r = vfs_mount(mount_point, &fat32_vfs_ops, 0);
    if (r == 0) { serial_write("vfs-fat32: FAT32 '"); serial_write(mount_point); serial_write("' noktasina baglandi\n"); }
    return r;
}
