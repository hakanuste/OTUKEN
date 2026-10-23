#ifndef OTUKENOS_RTC_H
#define OTUKENOS_RTC_H
#include <stdint.h>
#include "syscall.h"     /* sys_zaman_t */

/* 1.4.9 — gerçek saat.
 * CMOS RTC UTC kabul edilir (QEMU varsayılanı; VirtualBox --rtcuseutc on).
 * Açılışta bir kez okunur, sonra zamanlayıcı tikiyle ilerletilir (saatte
 * bir yeniden eşitlenir): ekran çizimi her saniye G/Ç kapısına gitmez. */
int      rtc_oku(sys_zaman_t *z);                  /* doğrudan CMOS: 0 / -1 */
void     zaman_baslat(void);                       /* açılışta */
void     zaman_esitle_gerekirse(void);             /* ana döngü */
int      zaman_var(void);                          /* RTC okunabildi mi */
int64_t  zaman_epoch(void);                        /* UTC, 1970'ten saniye; yoksa -1 */
/* epoch saniyesi -> takvim (dilim_dk eklenerek). hafta_gunu: 0=Pazartesi */
void     zaman_coz(int64_t epoch, int dilim_dk, sys_zaman_t *z, int *hafta_gunu);
int64_t  zaman_epoch_yap(const sys_zaman_t *z);

/* Yerel saat dilimi (dakika). Türkiye 2016'dan beri yaz saati uygulamaz: UTC+3. */
#ifndef OTUKEN_ZAMAN_DILIMI_DK
#define OTUKEN_ZAMAN_DILIMI_DK 180
#endif
#endif
