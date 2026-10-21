#ifndef OTUKENOS_TORE_H
#define OTUKENOS_TORE_H

#include <stdint.h>

/* TORE - otukenOS guvenlik politikalari ve yetki yonetimi (KUT).
 * Her surece bir yetki kumesi (capability set) atanir; ayricalikli
 * islemler bu kume uzerinden denetlenir. */

typedef enum {
    KUT_DISK_READ    = (1 << 0),
    KUT_DISK_WRITE   = (1 << 1),
    KUT_NET_SEND     = (1 << 2),
    KUT_NET_RECV     = (1 << 3),
    KUT_MEM_ALLOC    = (1 << 4),
    KUT_PROC_CREATE  = (1 << 5),
    KUT_PROC_KILL    = (1 << 6),
    KUT_HW_DIRECT    = (1 << 7)   /* dogrudan donanim erisimi */
} kut_capability_t;

/* Onceden tanimli yetki profilleri */
#define TORE_PROFILE_CEKIRDEK  0xFF   /* tam yetki */
#define TORE_PROFILE_HIZMET    (KUT_DISK_READ | KUT_NET_SEND | KUT_NET_RECV | KUT_MEM_ALLOC)
#define TORE_PROFILE_KULLANICI (KUT_DISK_READ | KUT_MEM_ALLOC)
#define TORE_PROFILE_KISITLI   (KUT_MEM_ALLOC)

void tore_init(void);

/* Bir surece (pid) yetki profili atar. */
int tore_set_capabilities(uint32_t pid, uint32_t capabilities);

/* Surecin belirtilen yetkiye sahip olup olmadigini denetler.
 * 1=izinli, 0=REDDEDILDI (ve reddedilenler loglanir). */
int tore_check(uint32_t pid, kut_capability_t cap, const char *action);

uint32_t tore_get_capabilities(uint32_t pid);
uint32_t tore_denied_count(void);
const char *tore_capability_name(kut_capability_t cap);

#endif
