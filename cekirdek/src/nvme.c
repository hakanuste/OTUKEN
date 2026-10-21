#include "nvme.h"
#include "pci.h"
#include "vmm.h"
#include "serial.h"

#define NVME_REG_CAP   0x00  /* yetenekler (64-bit) */
#define NVME_REG_VS    0x08  /* surum */
#define NVME_REG_CC    0x14  /* denetleyici yapilandirmasi */
#define NVME_REG_CSTS  0x1C  /* denetleyici durumu */

#define NVME_VIRT_BASE 0xA10000000ULL

static nvme_controller_t ctrl;

static uint32_t mmio_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t *)(uintptr_t)(base + offset);
}
static uint64_t mmio_read64(uint64_t base, uint32_t offset) {
    uint64_t lo = mmio_read32(base, offset);
    uint64_t hi = mmio_read32(base, offset + 4);
    return (hi << 32) | lo;
}

int nvme_init(void) {
    ctrl.present = 0;

    const pci_device_t *dev = pci_find_by_class(0x01, 0x08); /* NVMe */
    if (!dev) {
        serial_write("nvme: PCI'de NVMe denetleyici yok\n");
        return -1;
    }

    /* NVMe MMIO tabani BAR0/BAR1 (64-bit BAR olabilir) */
    uint64_t mmio_phys = (uint64_t)(dev->bar[0] & 0xFFFFFFF0);
    if ((dev->bar[0] & 0x6) == 0x4) { /* 64-bit BAR isaretcisi */
        mmio_phys |= ((uint64_t)dev->bar[1]) << 32;
    }
    if (mmio_phys == 0) {
        serial_write("nvme: BAR0 bos - denetleyici yapilandirilmamis\n");
        return -1;
    }

    for (int i = 0; i < 2; i++) { /* 8KB: kayitlar + ilk doorbell'lar */
        if (vmm_map_page(NVME_VIRT_BASE + (uint64_t)i * 4096,
                          mmio_phys + (uint64_t)i * 4096,
                          VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
            serial_write("nvme: MMIO haritalama basarisiz\n");
            return -1;
        }
    }

    ctrl.mmio_base         = NVME_VIRT_BASE;
    ctrl.capabilities      = mmio_read64(NVME_VIRT_BASE, NVME_REG_CAP);
    ctrl.version           = mmio_read32(NVME_VIRT_BASE, NVME_REG_VS);
    ctrl.controller_config = mmio_read32(NVME_VIRT_BASE, NVME_REG_CC);
    ctrl.controller_status = mmio_read32(NVME_VIRT_BASE, NVME_REG_CSTS);
    ctrl.max_queue_entries = (uint32_t)((ctrl.capabilities & 0xFFFF) + 1);
    ctrl.doorbell_stride   = (uint32_t)((ctrl.capabilities >> 32) & 0x0F);
    ctrl.ready             = (ctrl.controller_status & 1) ? 1 : 0;
    ctrl.present           = 1;

    serial_write("nvme: denetleyici bulundu, MMIO(fiziksel)=");
    serial_write_hex((uint32_t)mmio_phys);
    serial_write(" surum=");
    serial_write_hex(ctrl.version);
    serial_write("\n");
    serial_write("nvme: max_kuyruk_girisi=");
    serial_write_hex(ctrl.max_queue_entries);
    serial_write(" doorbell_stride=");
    serial_write_hex(ctrl.doorbell_stride);
    serial_write(" CSTS.RDY=");
    serial_write_hex((uint32_t)ctrl.ready);
    serial_write("\n");
    return 0;
}

const nvme_controller_t *nvme_get_controller(void) {
    return ctrl.present ? &ctrl : 0;
}
