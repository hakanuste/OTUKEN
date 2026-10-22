#ifndef OTUKENOS_KUT_H
#define OTUKENOS_KUT_H

#include <stdint.h>
#include "tore.h"

/* --- KUT: Kimlik ve Yetki (Asama 30) ---
 * TORE politika motorunun uzerine KULLANICI kimligi katmani ekler.
 * Her surecin bir sahibi (kullanici) vardir; yetkiler kullanicinin
 * rolunden turetilir. */

#define KUT_MAX_USERS 8
#define KUT_NAME_LEN  16

typedef enum {
    KUT_ROL_YONETICI = 0,   /* tam yetki */
    KUT_ROL_HIZMET   = 1,   /* sistem hizmetleri */
    KUT_ROL_KULLANICI= 2,   /* normal kullanici */
    KUT_ROL_KONUK    = 3    /* en kisitli */
} kut_rol_t;

typedef struct {
    int       used;
    uint32_t  uid;
    char      name[KUT_NAME_LEN];
    kut_rol_t rol;
} kut_user_t;

void kut_init(void);

/* Yeni kullanici tanimlar, uid dondurur (hata: 0). */
uint32_t kut_add_user(const char *name, kut_rol_t rol);

/* Sureci bir kullaniciya baglar ve rolune uygun yetkileri TORE'ye
 * yazar. Basari: 0 */
int kut_bind_process(uint32_t pid, uint32_t uid);

/* Surecin sahibi olan kullaniciyi dondurur (yoksa 0). */
const kut_user_t *kut_owner_of(uint32_t pid);
const kut_user_t *kut_find_user(uint32_t uid);
/* 1.4.9: salt-okunur doku (KUT uygulamasi) */
int kut_kullanici_al(int i, kut_user_t *out);
int kut_bag_al(int i, uint32_t *pid, uint32_t *uid);
const char *kut_rol_name(kut_rol_t r);
uint32_t kut_capabilities_for_role(kut_rol_t r);

#endif
