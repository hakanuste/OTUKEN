#ifndef OTUKENOS_VFS_FAT16_H
#define OTUKENOS_VFS_FAT16_H

#include "vfs.h"

/* FAT16'yi VFS'e "/" noktasinda baglar. tss_init gibi, disk_init +
 * fat16_mount BASARIYLA tamamlandiktan SONRA cagrilmalidir. Basari:0,
 * hata:-1. */
int vfs_fat16_mount_root(void);

#endif
