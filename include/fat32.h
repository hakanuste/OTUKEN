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
int fat32_mount(void);
int fat32_list_root(fat32_dirent_t *out, int max);
int fat32_read_file_at(const fat32_dirent_t *entry, uint8_t *buf, uint32_t bufsize, uint32_t offset);
#endif
