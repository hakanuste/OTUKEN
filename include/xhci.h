#ifndef OTUKENOS_XHCI_H
#define OTUKENOS_XHCI_H
/* 1.4.9: XHCI aktarim katmani + USB HID klavye/fare/tablet (src/xhci.c) */
int  xhci_init(void);          /* HID aygit sayisi; denetleyici yoksa -1 */
int  xhci_hazir(void);
int  xhci_aygit_sayisi(void);
void xhci_yokla(void);         /* ana dongu: olaylari isle, tus yinelemesi */
#endif
