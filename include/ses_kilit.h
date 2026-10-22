#ifndef OTUKENOS_SES_KILIT_H
#define OTUKENOS_SES_KILIT_H
#include "mutex.h"
/* SMP adim 6: AC97 denetleyicisi ve ses arabellekleri (ses.c, dede.c, ozan_ses.c, ezgi.c).
 * Uzun islemler (ekran birlestirme / ses sentezi) -> ozyinelemeli UYUYAN
 * kilit; kesme baglaminda ya da spinlock tutarken ALINMAZ.
 * SIRA: gorsel_kilit -> depo_kilit ; ses_kilit -> depo_kilit. */
extern ozkilit_t ses_kilit;
#endif
