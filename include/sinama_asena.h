#ifndef OTUKENOS_SINAMA_ASENA_H
#define OTUKENOS_SINAMA_ASENA_H
/* 1.4.9 ASENA-TEST / ERGENEKON-TEST (yalniz TANI; bkz. src/sinama_asena.c, tests/asena) */
void sinama_asena_baslat(void);
int  sinama_asena_mod(void);     /* 0 yok, 1 ASENA, 2 ERGENEKON */
int  sinama_asena_bitti(void);   /* ekranı kullanan ana bölüm bitti (ya da atlandı) */
#endif
