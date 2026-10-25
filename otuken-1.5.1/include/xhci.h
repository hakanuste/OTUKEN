#ifndef OTUKENOS_XHCI_H
#define OTUKENOS_XHCI_H
#include <stdint.h>

/* --- XHCI (USB 3.x) aktarim katmani ---
 * usb.c yalnizca denetleyiciyi TESPIT eder; UHCI surucusu ise yalnizca
 * USB 1.1 icindir. Guncel bilgisayarlarin tamaminda USB portlari XHCI
 * uzerindedir. Burada gercek XHCI surucusu vardir: BIOS/UEFI'den sahiplik
 * devri, denetleyici sifirlama, komut/olay/aktarim halkalari, cihaz
 * numaralandirma ve USB HID BOOT klavyesi.
 *
 * Kapsam disi (acikca): USB hub arkasindaki cihazlar, USB fare,
 * MSI/MSI-X kesmesi (olay halkasi yoklanir). */

int  xhci_init(void);          /* denetleyiciyi bul, klavye varsa numaralandir */
int  xhci_var(void);           /* en az bir XHCI denetleyici baslatildi mi */
int  xhci_klavye_var(void);
void xhci_yokla(void);         /* bloklamaz; yeni tuslari PS/2 kuyruguna besler */

#endif
