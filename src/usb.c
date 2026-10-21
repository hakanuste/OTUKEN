#include "usb.h"
#include "pci.h"
#include "vmm.h"
#include "io.h"
#include "log.h"
#include "serial.h"

/* --- XHCI (USB3) yetenek kayitlari --- */
#define XHCI_CAPLENGTH  0x00
#define XHCI_HCIVERSION 0x02
#define XHCI_HCSPARAMS1 0x04
/* Calisma (operational) kayitlari CAPLENGTH kadar ileride baslar */
#define XHCI_PORTSC(n)  (0x400 + (n) * 0x10)

/* --- EHCI (USB2) --- */
#define EHCI_CAPLENGTH  0x00
#define EHCI_HCIVERSION 0x02
#define EHCI_HCSPARAMS  0x04
#define EHCI_PORTSC0    0x44

/* --- UHCI (USB1.1, I/O portu) --- */
#define UHCI_USBCMD     0x00
#define UHCI_PORTSC1    0x10

#define USB_VIRT_TABAN 0xD00000000ULL

static usb_denetleyici_t dts[USB_MAX_CONTROLLERS];
static int sayi = 0;

const char *usb_tur_adi(usb_tur_t t) {
    switch (t) {
        case USB_UHCI: return "UHCI (USB 1.1)";
        case USB_OHCI: return "OHCI (USB 1.1)";
        case USB_EHCI: return "EHCI (USB 2.0)";
        default:       return "XHCI (USB 3.x)";
    }
}

static uint32_t mmio32(uint64_t taban, uint32_t ofs) {
    return *(volatile uint32_t *)(uintptr_t)(taban + ofs);
}
static uint16_t mmio16(uint64_t taban, uint32_t ofs) {
    return *(volatile uint16_t *)(uintptr_t)(taban + ofs);
}
static uint8_t mmio8(uint64_t taban, uint32_t ofs) {
    return *(volatile uint8_t *)(uintptr_t)(taban + ofs);
}

static void xhci_incele(usb_denetleyici_t *d) {
    /* CAPLENGTH (8 bit) ve HCIVERSION (16 bit) AYNI 32-bit kaydin
     * icindedir. Bazi denetleyiciler 16-bit hizasiz okumayi dogru
     * yanitlamaz; tek 32-bit okuyup alanlari ayirmak guvenlidir. */
    uint32_t cap0 = mmio32(d->taban, 0x00);
    uint8_t caplen = (uint8_t)(cap0 & 0xFF);
    d->surum = (uint16_t)((cap0 >> 16) & 0xFFFF);
    uint32_t hcs1 = mmio32(d->taban, XHCI_HCSPARAMS1);
    d->port_sayisi = (int)((hcs1 >> 24) & 0xFF);

    /* Her portun PORTSC kaydinda CCS (bit0) = cihaz bagli mi */
    d->bagli_port = 0;
    for (int p = 0; p < d->port_sayisi && p < 32; p++) {
        uint32_t portsc = mmio32(d->taban, (uint32_t)(caplen + XHCI_PORTSC(p)));
        if (portsc & 1) d->bagli_port++;
    }
}

static void ehci_incele(usb_denetleyici_t *d) {
    uint8_t caplen = mmio8(d->taban, EHCI_CAPLENGTH);
    d->surum = mmio16(d->taban, EHCI_HCIVERSION);
    uint32_t hcs = mmio32(d->taban, EHCI_HCSPARAMS);
    d->port_sayisi = (int)(hcs & 0x0F);

    d->bagli_port = 0;
    for (int p = 0; p < d->port_sayisi && p < 16; p++) {
        uint32_t portsc = mmio32(d->taban, (uint32_t)(caplen + 0x44 + p * 4));
        if (portsc & 1) d->bagli_port++;   /* Current Connect Status */
    }
}

static void uhci_incele(usb_denetleyici_t *d) {
    /* UHCI MMIO degil, I/O portu kullanir - sabit 2 kok port */
    d->surum = 0x0110;
    d->port_sayisi = 2;
    d->bagli_port = 0;
    for (int p = 0; p < 2; p++) {
        uint16_t portsc = inw((uint16_t)(d->taban + UHCI_PORTSC1 + p * 2));
        if (portsc & 1) d->bagli_port++;
    }
}

int usb_init(void) {
    sayi = 0;
    uint64_t sonraki_virt = USB_VIRT_TABAN;

    /* PCI sinifi 0x0C (seri veri yolu), alt sinif 0x03 (USB) */
    for (int i = 0; i < pci_device_count() && sayi < USB_MAX_CONTROLLERS; i++) {
        const pci_device_t *p = pci_get_device(i);
        if (p->class_code != 0x0C || p->subclass != 0x03) continue;

        usb_denetleyici_t *d = &dts[sayi];
        d->var = 1;
        d->vendor_id = p->vendor_id;
        d->device_id = p->device_id;

        switch (p->prog_if) {
            case 0x00: d->tur = USB_UHCI; break;
            case 0x10: d->tur = USB_OHCI; break;
            case 0x20: d->tur = USB_EHCI; break;
            default:   d->tur = USB_XHCI; break;
        }

        if (d->tur == USB_UHCI) {
            /* UHCI I/O alanini BAR4'te tutar (alt bit 1 = I/O alani) */
            d->mmio = 0;
            d->taban = (uint64_t)(p->bar[4] & ~0x3u);
            uhci_incele(d);
        } else {
            d->mmio = 1;
            uint64_t fiz = (uint64_t)(p->bar[0] & 0xFFFFFFF0);
            if ((p->bar[0] & 0x6) == 0x4) fiz |= ((uint64_t)p->bar[1]) << 32;
            if (fiz == 0) { d->var = 0; continue; }

            /* MMIO'yu onbelleksiz (PCD) haritala */
            for (int s = 0; s < 4; s++) {
                if (vmm_map_page(sonraki_virt + (uint64_t)s * 4096,
                                  fiz + (uint64_t)s * 4096,
                                  VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
                    d->var = 0; break;
                }
            }
            if (!d->var) continue;
            d->taban = sonraki_virt;
            sonraki_virt += 0x10000;

            if (d->tur == USB_XHCI) xhci_incele(d);
            else if (d->tur == USB_EHCI) ehci_incele(d);
            else { d->surum = 0x0110; d->port_sayisi = 0; d->bagli_port = 0; }
        }

        serial_write("USB: ");
        serial_write(usb_tur_adi(d->tur));
        serial_write(" denetleyici vendor=");
        serial_write_hex(d->vendor_id);
        serial_write(" device=");
        serial_write_hex(d->device_id);
        serial_write(" surum=");
        serial_write_hex(d->surum);
        serial_write(" port=");
        serial_write_hex((uint32_t)d->port_sayisi);
        serial_write(" bagli=");
        serial_write_hex((uint32_t)d->bagli_port);
        serial_write("\n");

        sayi++;
    }

    if (sayi == 0) {
        log_write(LOG_BILGI, "USB", "USB denetleyici bulunamadi");
    } else {
        log_write_num(LOG_BILGI, "USB", "denetleyici sayisi", (uint64_t)sayi);
    }
    return sayi;
}

int usb_denetleyici_sayisi(void) { return sayi; }

const usb_denetleyici_t *usb_denetleyici_al(int i) {
    if (i < 0 || i >= sayi) return 0;
    return &dts[i];
}
