#ifndef OTUKENOS_ISKENCE_H
#define OTUKENOS_ISKENCE_H
#include <stdint.h>

/* --- Acimasiz dayaniklilik sinamalari ---
 * Sistemi KASITLI olarak kotu girdilerle, tukenme kosullariyla ve
 * sinir degerlerle zorlar. Amac: cokmeden, sizinti birakmadan ve
 * dogru hata dondurerek hayatta kalmak. */

/* Donus: tespit edilen HATA sayisi (0 = kusursuz). */
int iskence_syscall(void);      /* gecersiz numaralar, kotu isaretciler */
int iskence_bellek(void);       /* tukenme, sinir, sifir, cift free */
int iskence_dosya(void);        /* olmayan dosya, bos ad, uzun ad */
int iskence_ag(void);           /* sifir uzunluk, azami boyut, kotu port */
int iskence_ipc(void);          /* dolu kuyruk, gecersiz port, tasma */
int iskence_gorev(void);        /* gorev tukenmesi, cift sonlandirma */
int iskence_cr3(void);          /* CR3 degisimi altinda isaretci guvenligi */
int iskence_tumu(void);         /* hepsini calistirir, toplam hata doner */

#endif
