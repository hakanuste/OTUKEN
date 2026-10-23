#ifndef OTUKENOS_SINAMA149_H
#define OTUKENOS_SINAMA149_H
/* 1.4.9 AG-TEST (yalniz TANI): diskte AGSINA.ELF varsa Ring3 ag sinamasini
 * kendi surecinde kosturur, bitince tutamak/yuva sizintisini denetler. */
void sinama149_ag_baslat(void);
int  sinama149_bitti(void);          /* AG/TULPAR sinamasi bitti (ya da atlandi) */
#include <stdint.h>
/* yuzeyin SUNULAN karesinden bir satiri glif glif okur (UTF-8). Dönüş: bayt */
int  sinama_satir_ocr(int id, int x0, int y0, int hucre, uint32_t fg, uint32_t bg, char *o, int on);
/* ayni okuma, EKRANIN kendisinden (VRAM; ust cubuk ve menu icin) */
int  sinama_ekran_ocr(int x0, int y0, int hucre, uint32_t fg, uint32_t bg, char *o, int on);
#endif
