#ifndef OTUKENOS_EKRAN_H
#define OTUKENOS_EKRAN_H
#include <stdint.h>
/* =====================================================================
 * EKRAN BIRLESTIRICI (1.4.8) — masaustundeki TEK cizim sahibi.
 *
 * Eskiden iki ayri birlestirici ayni ekrana birbirinden habersiz
 * yaziyordu: gui.c (Terminal/Dosya/Goruntuleyici/Gunluk; kendi zorder'i)
 * ve ULGEN (UMAY vb. yuzeyler; kendi z'si). Hangisi en son cizdiyse ustte
 * kaliyordu; gui.c tam cizimi ULGEN yuzeylerini siliyor, ULGEN altindaki
 * alani bilmeden ciziyordu. Simdi:
 *   - TEK z-sayaci (ulgen_z_ver): GUI pencereleri ve ULGEN yuzeyleri ayni
 *     sirada; one getirilen, hangi sistemden olursa olsun ustte.
 *   - HASAR: degisen dikdortgen isaretlenir; ekran_isle() o bolgeyi
 *     masaustu -> z-sirasiyla butun ogeler -> gorev cubugu -> imlec
 *     olarak YENIDEN cizer. Her oge kendi dikdortgenine kirpilir.
 * Cagiran gorsel_kilit'i tutmalidir.
 * ===================================================================== */
#define EKRAN_MASAUSTU_RENK 0x123B4Au

void ekran_hasar(int x, int y, int w, int h);
void ekran_tam_hasar(void);
int  ekran_hasar_var(void);
void ekran_isle(void);

/* Masaustu katmani: temadan bir kez cizilir (acilis, tema degisimi). */
void     ekran_masaustu_hazirla(void);
int      ekran_masaustu_var(void);
uint32_t ekran_masaustu_pikseli(int x, int y);   /* sinama */

#endif
