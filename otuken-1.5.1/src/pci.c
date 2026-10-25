#include "pci.h"
#include "io.h"
#include "serial.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static pci_device_t devices[PCI_MAX_DEVICES];
static int device_count = 0;

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = ((uint32_t)1 << 31)          /* enable biti */
                     | ((uint32_t)bus  << 16)
                     | ((uint32_t)slot << 11)
                     | ((uint32_t)func << 8)
                     | ((uint32_t)offset & 0xFC);   /* 4 bayt hizali */
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = ((uint32_t)1 << 31)
                     | ((uint32_t)bus  << 16)
                     | ((uint32_t)slot << 11)
                     | ((uint32_t)func << 8)
                     | ((uint32_t)offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static void read_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *d) {
    uint32_t reg0 = pci_config_read32(bus, slot, func, 0x00);
    uint32_t reg2 = pci_config_read32(bus, slot, func, 0x08);
    uint32_t reg3 = pci_config_read32(bus, slot, func, 0x0C);
    uint32_t regF = pci_config_read32(bus, slot, func, 0x3C);

    d->bus = bus; d->slot = slot; d->func = func;
    d->vendor_id  = (uint16_t)(reg0 & 0xFFFF);
    d->device_id  = (uint16_t)(reg0 >> 16);
    d->prog_if    = (uint8_t)((reg2 >> 8) & 0xFF);
    d->subclass   = (uint8_t)((reg2 >> 16) & 0xFF);
    d->class_code = (uint8_t)((reg2 >> 24) & 0xFF);
    d->header_type = (uint8_t)((reg3 >> 16) & 0xFF);
    d->irq_line   = (uint8_t)(regF & 0xFF);

    for (int i = 0; i < 6; i++)
        d->bar[i] = pci_config_read32(bus, slot, func, (uint8_t)(0x10 + i * 4));
}

int pci_scan(void) {
    device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t reg0 = pci_config_read32((uint8_t)bus, slot, 0, 0x00);
            uint16_t vendor = (uint16_t)(reg0 & 0xFFFF);
            if (vendor == 0xFFFF) continue; /* cihaz yok */

            uint32_t reg3 = pci_config_read32((uint8_t)bus, slot, 0, 0x0C);
            uint8_t header_type = (uint8_t)((reg3 >> 16) & 0xFF);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1; /* cok fonksiyonlu mu */

            for (uint8_t func = 0; func < max_func; func++) {
                uint32_t f0 = pci_config_read32((uint8_t)bus, slot, func, 0x00);
                if ((uint16_t)(f0 & 0xFFFF) == 0xFFFF) continue;
                if (device_count >= PCI_MAX_DEVICES) {
                    serial_write("pci: UYARI - cihaz tablosu doldu, tarama kesildi\n");
                    return device_count;
                }
                read_device((uint8_t)bus, slot, func, &devices[device_count]);
                device_count++;
            }
        }
    }
    return device_count;
}

int pci_device_count(void) { return device_count; }

const pci_device_t *pci_get_device(int index) {
    if (index < 0 || index >= device_count) return 0;
    return &devices[index];
}

const pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < device_count; i++)
        if (devices[i].class_code == class_code && devices[i].subclass == subclass)
            return &devices[i];
    return 0;
}

const pci_device_t *pci_find_by_id(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < device_count; i++)
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id)
            return &devices[i];
    return 0;
}

const char *pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x01:
            if (subclass == 0x01) return "IDE denetleyici";
            if (subclass == 0x06) return "SATA/AHCI denetleyici";
            if (subclass == 0x08) return "NVMe denetleyici";
            return "Depolama denetleyici";
        case 0x02: return "Ag denetleyici";
        case 0x03: return "Ekran/VGA denetleyici";
        case 0x04: return "Multimedya";
        case 0x06:
            if (subclass == 0x00) return "Host kopru (bridge)";
            if (subclass == 0x01) return "ISA kopru";
            return "Kopru (bridge)";
        case 0x0C:
            if (subclass == 0x03) return "USB denetleyici";
            return "Seri veri yolu";
        default: return "Bilinmeyen";
    }
}
