#ifndef OTUKENOS_GORSEL_KILIT_H
#define OTUKENOS_GORSEL_KILIT_H
#include "mutex.h"
/* SMP adim 6: ULGEN birlestirici + GUI terminali/pencereleri (gui.c) ORTAK durumu.
 * Uzun islemler (ekran birlestirme / ses sentezi) -> ozyinelemeli UYUYAN
 * kilit; kesme baglaminda ya da spinlock tutarken ALINMAZ.
 * SIRA: gorsel_kilit -> depo_kilit ; ses_kilit -> depo_kilit. */
extern ozkilit_t gorsel_kilit;
#endif
