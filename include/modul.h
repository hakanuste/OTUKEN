#ifndef OTUKENOS_MODUL_H
#define OTUKENOS_MODUL_H
/* 1.4.9: önyükleyicinin (GRUB) belleğe yüklediği modüller. ERGENEKON'un
 * dizge imajı (/boot/ergenekon.krv) böyle gelir: CD'den okuma sürücüsü
 * gerekmez. Modül çerçeveleri PMM'de ayrılmış işaretlenir. */
#include <stdint.h>
#include "multiboot.h"

#define MODUL_AZAMI 4
typedef struct { uint64_t bas, son; char ad[48]; } onyukleme_modul_t;

void modul_ekle(uint64_t bas, uint64_t son, const char *ad);
void modul_mb1_oku(const multiboot_info_t *mbi);   /* multiboot1 modül listesi */
int  modul_sayisi(void);
const onyukleme_modul_t *modul_al(int i);
/* adında 'ad' geçen ilk modül; 0 / -1 */
int  modul_bul(const char *ad, const uint8_t **veri, uint32_t *boy);
#endif
