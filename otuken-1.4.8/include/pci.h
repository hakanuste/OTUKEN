#ifndef OTUKENOS_PCI_H
#define OTUKENOS_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 32

typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  irq_line;
} pci_device_t;

/* Tum PCI veri yolunu tarar, bulunan cihazlari ic tabloya kaydeder.
 * Bulunan cihaz sayisini dondurur. */
int pci_scan(void);

int pci_device_count(void);
const pci_device_t *pci_get_device(int index);

/* Belirli bir sinif/alt-sinif ile eslesen ILK cihazi dondurur
 * (bulunamazsa 0) - surucu eslestirmesi (Stage 15 depolama) icin. */
const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass);

/* Vendor/device ID ile arama. */
const pci_device_t *pci_find_by_id(uint16_t vendor_id, uint16_t device_id);

const char *pci_class_name(uint8_t class_code, uint8_t subclass);

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

#endif
