#ifndef OTUKENOS_MULTIBOOT_H
#define OTUKENOS_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Multiboot1 bilgi yapisi (ihtiyacimiz olan alanlar) */
typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    /* GRUB'un kendi yapisinda renk bilgisi bir BIRLIKtir (union: palet
     * icin uint32 + uint16); bu birlik 4 bayta hizalanir. Belirtim 110.
     * bayti yazar ama GRUB alanlari 112'den baslatir (olculdu: ham
     * baytlar 110-111 = 0, 112 = kirmizi konumu 16). Bu dolgu olmadan
     * kirmizi/yesil/mavi konumlari kayar ve renkler bozuk cikar. */
    uint8_t  framebuffer_dolgu[2];
    uint8_t  color_info[6];
} __attribute__((packed)) multiboot_info_t;

#define MULTIBOOT_INFO_FRAMEBUFFER_INFO 0x1000
#define MULTIBOOT_INFO_MEM_MAP          0x0040
#define MULTIBOOT_INFO_MEMORY           0x0002

typedef struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

#define MULTIBOOT_MEMORY_AVAILABLE 1

#endif
