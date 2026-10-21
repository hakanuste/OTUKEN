#ifndef OTUKENOS_AHCI_H
#define OTUKENOS_AHCI_H

#include <stdint.h>

/* AHCI (Advanced Host Controller Interface) hazirligi.
 * Bu asamada: denetleyiciyi PCI uzerinden bulur, MMIO tabanini (ABAR)
 * haritalar, yetenek/port kayitlarini okur ve bagli surucuku portlari
 * tespit eder. TAM veri okuma (komut listesi/FIS/DMA) sonraki asamaya
 * birakildi - burada dogru donanim tespiti ve kayit erisimi kuruluyor. */

#define AHCI_MAX_PORTS 32

typedef struct {
    int      present;          /* denetleyici bulundu mu */
    uint64_t abar;             /* MMIO taban adresi */
    uint32_t capabilities;
    uint32_t global_host_ctrl;
    uint32_t ports_implemented;
    uint32_t version;
    int      num_ports;        /* desteklenen port sayisi */
    int      active_ports[AHCI_MAX_PORTS]; /* bagli surucu var mi */
    int      active_port_count;
} ahci_controller_t;

int ahci_init(void);

/* --- Gercek veri okuma (DMA) ---
 * Belirtilen porttan LBA adresinden 'sayi' sektor okur.
 * Basari: 0, hata: -1. Port, ahci_init tarafindan AKTIF isaretlenmis
 * olmalidir. */
int ahci_oku(int port, uint64_t lba, uint32_t sayi, void *tampon);

/* Portu komut almaya hazirlar (komut listesi + FIS alani ayirir).
 * ahci_init icinde aktif portlar icin otomatik cagrilir. */
int ahci_port_hazirla(int port);

/* Yazma (WRITE DMA EXT + FLUSH CACHE EXT), en fazla 8 sektor/cagri. */
int ahci_yaz(int port, uint64_t lba, uint32_t sayi, const void *tampon);
/* IDENTIFY ile toplam sektor sayisi. 0 basari. */
int ahci_sektor_sayisi(int port, uint64_t *sektor);
/* sira'inci SATA disk portu (ATAPI haric), yoksa -1. */
int ahci_disk_portu(int sira);
const ahci_controller_t *ahci_get_controller(void);

#endif
