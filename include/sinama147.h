#ifndef OTUKENOS_SINAMA147_H
#define OTUKENOS_SINAMA147_H
/* 1.4.7 cekirdek regresyon sinamalari (yalniz TANI kipi). Ayrinti:
 * src/sinama147.c. Her biri basarisizlik sayisini doner. */

/* AP dagitimindan ONCE (tek cekirdek): ASYOK, FD, POLITIKA, YUZEY, TOREREAP. */
int sinama147_tek_cekirdek(void);

/* AP dagitimindan SONRA: SUREC-SMP (calisan surecin yok edilmesi). */
int sinama147_surec_smp(void);
#endif
