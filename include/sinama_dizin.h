#ifndef OTUKENOS_SINAMA_DIZIN_H
#define OTUKENOS_SINAMA_DIZIN_H
#include <stdint.h>
/* 1.4.9 DIZIN-YAZ-TEST (yalniz TANI; bkz. src/sinama_dizin.c) */
void sinama_dizin_baslat(void);
/* UMAY acilista /KUT/UMAY'i yaratip GUNLUK.TXT'ye yazar; bos kume sayan
 * sinamalar bu yazim durulana kadar bekler (dosya var ve 2 sn boyu
 * degismiyor, en cok azami_sn). 1: durgun, 0: zaman asimi. */
int  sinama_umay_gunluk_durgun(uint32_t azami_sn);
#endif
