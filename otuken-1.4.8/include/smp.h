#ifndef OTUKENOS_SMP_H
#define OTUKENOS_SMP_H

#include <stdint.h>

/* --- SMP adim 3: APIC + AP baslatma ---
 * ACPI MADT'den mantiksal cekirdekleri bulur, yerel APIC'i hazirlar
 * ve her AP'yi INIT-SIPI-SIPI ile tek tek uyandirir. Her AP kendi
 * cekirdek yiginiyla 64-bit moda gecer, seri porta "uyandi" yazar ve
 * kesmeler KAPALI olarak hlt'de bekler. Is dagitimi bu adimda YOK.
 *
 * Kesmeler acilmadan (sti) ONCE, TIMUR (EFER.NXE) ve CR0.WP
 * kurulduktan SONRA cagrilmalidir: AP bu kayitlari BSP'den kopyalar. */

#define SMP_AZAMI 64

void     smp_baslat(void);
int      smp_bulunan(void);      /* MADT'de etkin cekirdek sayisi (BSP dahil) */
int      smp_uyanan(void);       /* calisan cekirdek sayisi (BSP dahil) */
int      smp_x2apic(void);       /* 1: firmware x2APIC kipinde birakti */
uint32_t lapic_kimlik(void);     /* cagiran cekirdegin APIC kimligi */

/* --- SMP adim 4 ---
 * Diger cevrimici cekirdeklerde fn(arg) calistirir (IPI 0xF0) ve
 * bitmelerini bekler. Tek cekirdekte hicbir sey yapmaz. */
int  smp_cagir_digerleri(void (*fn)(uint64_t), uint64_t arg);
/* TLB vurusu: tlb_uzak_vur yalniz DIGER cekirdekleri temizler (yerel
 * invlpg'yi cagiran yapmis olmali); tlb_vur ikisini birden yapar.
 * Yalniz ONCEDEN GECERLI bir girdi degistiginde gerekir. */
void tlb_uzak_vur(uint64_t sanal);
void tlb_vur(uint64_t sanal);
void tlb_vur_hepsi(void);

/* --- SMP adim 5: is dagitimi --- */
void smp_bekleyen_isle(void);   /* IF=0 spin donguleri cagirir */
void smp_dagitimi_ac(void);     /* AP'ler zamanlayiciya katilir */
int  smp_dagitim_acik(void);

#endif
