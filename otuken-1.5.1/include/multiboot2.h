#ifndef OTUKENOS_MULTIBOOT2_H
#define OTUKENOS_MULTIBOOT2_H

#include <stdint.h>
#include "multiboot.h"

/* --- Multiboot2 destegi (UEFI icin SART) ---
 * GRUB'in UEFI surumu yalnizca multiboot2 anlar. Bilgi yapisi
 * multiboot1'den TAMAMEN FARKLIDIR: sabit alanlar yerine
 * birbirini izleyen ETIKETLER (tag) dizisidir.
 *
 * Bu katman, multiboot2 etiketlerini okuyup cekirdegin zaten
 * kullandigi multiboot1 yapisina CEVIRIR - boylece cekirdegin
 * geri kalani degismek zorunda kalmaz. */

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

#define MB2_TAG_SON          0
#define MB2_TAG_BELLEK       4   /* temel bellek bilgisi */
#define MB2_TAG_MMAP         6   /* bellek haritasi */
#define MB2_TAG_FRAMEBUFFER  8
#define MB2_TAG_ACPI_ESKI    14  /* RSDP v1 kopyasi */
#define MB2_TAG_ACPI_YENI    15  /* RSDP v2 kopyasi */

typedef struct {
    uint32_t tur;
    uint32_t boyut;
} mb2_etiket_t;

/* Multiboot2 bilgi yapisini multiboot1 bicimine cevirir.
 * Basari: 0, hata: -1 */
int multiboot2_cevir(uint64_t mb2_adres, multiboot_info_t *cikti);

/* Onyukleme protokolunu dondurur: 1=multiboot1, 2=multiboot2, 0=bilinmiyor */
int onyukleme_protokolu(uint32_t magic);

/* Cevrilen bellek haritasinin TAM 64-bit adresi (multiboot1'in
 * 32-bit mmap_addr alani higher-half adresleri tasiyamaz). */
uint64_t multiboot2_mmap_adresi(void);
uint32_t multiboot2_mmap_uzunlugu(void);

/* SMP adim 3: GRUB-UEFI, RSDP'yi BIOS alaninda birakmayabilir; bunun
 * yerine multiboot2 etiketi (14/15) olarak KOPYASINI verir. Kopya
 * burada saklanir. Etiket yoksa 0 doner. */
const uint8_t *multiboot2_rsdp(void);

#endif
