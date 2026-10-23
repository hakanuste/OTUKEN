#ifndef OTUKENOS_VMM_H
#define OTUKENOS_VMM_H

#include <stdint.h>

#define VMM_PRESENT  0x1ULL
#define VMM_WRITABLE 0x2ULL
#define VMM_USER     0x4ULL

void vmm_init(void);

/* virt adresini phys'e haritalar (4KB granularite). Gerekirse ara
 * tablolari (PDPT/PD/PT) PMM'den alir; virt zaten bir 2MB sayfanin
 * icindeyse o sayfayi otomatik olarak 512x4KB'ye "bolar" (mevcut
 * haritalamayi bozmadan). Basari:0, hata:-1 (PMM'den cerceve alinamadi). */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

void vmm_unmap_page(uint64_t virt);

/* virt haritali degilse 0 doner. */
uint64_t vmm_get_physical(uint64_t virt);

/* --- Asama 23: surec basina sanal adres alani ---
 * Her surec kendi PML4'une (sayfa tablosu koku) sahip olur. Cekirdek
 * bolgeleri (higher-half + kimlik haritasi) TUM adres alanlarinda
 * PAYLASILIR; kullanici bolgesi (PML4[1..510]) her surece OZELDIR.
 * Boylece iki surec AYNI sanal adresi kullanip FARKLI fiziksel
 * bellege bakabilir - gercek surec izolasyonu budur. */

/* Yeni bir adres alani olusturur, fiziksel PML4 adresini dondurur.
 * Hata: 0 */
uint64_t vmm_create_address_space(void);

/* CR3'u degistirerek verilen adres alanina gecer. */
void vmm_switch_address_space(uint64_t pml4_phys);

/* Su anki adres alaninin (CR3) fiziksel PML4 adresi. */
uint64_t vmm_current_address_space(void);

/* Adres alanini ve ona OZEL (kullanici) sayfa tablolarini serbest
 * birakir. Paylasilan cekirdek tablolarina DOKUNMAZ. */
void vmm_destroy_address_space(uint64_t pml4_phys);

/* 1.4.7: canli adres alani sayisi (olusturulan - yok edilen). */
int64_t vmm_canli_alan_sayisi(void);
/* 1.4.8 (sinama): canli adres alaninin olusturma sira no'su; yok edilmisse 0.
 * PML4 cercevesi yeniden kullanilirsa YENI sira no doner. */
uint64_t vmm_alan_kimlik(uint64_t pml4);

/* Belirtilen adres alaninda haritalama yapar (mevcut CR3'u
 * degistirmeden). */
int vmm_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

#endif
