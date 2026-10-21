#ifndef OTUKENOS_USB_H
#define OTUKENOS_USB_H

#include <stdint.h>

/* --- Asama 28: USB host denetleyici altyapisi ---
 * PCI uzerinden USB denetleyicilerini bulur, turlerini tanir ve
 * MMIO/IO kayitlarini okuyarak port durumlarini raporlar.
 * Kapsam: denetleyici tespiti, yetenek kayitlari, port sayimi ve
 * bagli cihaz algilama. Tam aktarim (transfer) katmani - kontrol/toplu
 * transferler, cihaz numaralandirma - bu asamanin DISINDADIR; AHCI ve
 * NVMe icin izlenen "hazirlik" yaklasiminin aynisi. */

#define USB_MAX_CONTROLLERS 4

typedef enum {
    USB_UHCI = 0,   /* USB 1.1, Intel   (prog_if 0x00) */
    USB_OHCI = 1,   /* USB 1.1, Compaq  (prog_if 0x10) */
    USB_EHCI = 2,   /* USB 2.0          (prog_if 0x20) */
    USB_XHCI = 3    /* USB 3.x          (prog_if 0x30) */
} usb_tur_t;

typedef struct {
    int        var;
    usb_tur_t  tur;
    uint16_t   vendor_id;
    uint16_t   device_id;
    uint64_t   taban;        /* MMIO tabani (EHCI/XHCI) ya da I/O tabani (UHCI) */
    int        mmio;         /* 1=MMIO, 0=I/O portu */
    uint32_t   surum;
    int        port_sayisi;
    int        bagli_port;   /* uzerinde cihaz algilanan port sayisi */
} usb_denetleyici_t;

int usb_init(void);
int usb_denetleyici_sayisi(void);
const usb_denetleyici_t *usb_denetleyici_al(int i);
const char *usb_tur_adi(usb_tur_t t);

#endif
