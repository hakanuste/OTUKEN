#ifndef OTUKENOS_SMAP_H
#define OTUKENOS_SMAP_H
/* 1.4.9: SMEP/SMAP. SMEP: cekirdek kullanici sayfasindaki kodu CALISTIRAMAZ.
 * SMAP: cekirdek kullanici sayfasina yalniz ACIKCA izin verilen yerde
 * (kullanici_ac ... kullanici_kapat; STAC/CLAC) dokunabilir. Islemci SMAP
 * desteklemiyorsa STAC/CLAC #UD verir: bu yuzden bayrakla korunur. */
extern volatile int g_smap_etkin;
static inline void kullanici_ac(void)    { if (g_smap_etkin) __asm__ volatile ("stac" ::: "memory", "cc"); }
static inline void kullanici_kapat(void) { if (g_smap_etkin) __asm__ volatile ("clac" ::: "memory", "cc"); }
/* acilista BSP'de (CR4.SMEP/SMAP); AP'ler CR4'u BSP'den alir. Donus: bit0 SMEP, bit1 SMAP */
int smep_smap_ac(void);
#endif
