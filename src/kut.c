#include "kut.h"
#include "log.h"
#include "serial.h"

#define KUT_MAX_BINDINGS 16

typedef struct { int used; uint32_t pid; uint32_t uid; } kut_binding_t;

static kut_user_t    users[KUT_MAX_USERS];
static kut_binding_t bindings[KUT_MAX_BINDINGS];
static uint32_t next_uid = 1;

void kut_init(void) {
    for (int i = 0; i < KUT_MAX_USERS; i++) users[i].used = 0;
    for (int i = 0; i < KUT_MAX_BINDINGS; i++) bindings[i].used = 0;
    next_uid = 1;
    log_write(LOG_BILGI, "KUT", "kimlik ve yetki katmani baslatildi");
}

const char *kut_rol_name(kut_rol_t r) {
    switch (r) {
        case KUT_ROL_YONETICI:  return "YONETICI";
        case KUT_ROL_HIZMET:    return "HIZMET";
        case KUT_ROL_KULLANICI: return "KULLANICI";
        default:                return "KONUK";
    }
}

uint32_t kut_capabilities_for_role(kut_rol_t r) {
    switch (r) {
        case KUT_ROL_YONETICI:  return TORE_PROFILE_CEKIRDEK;
        case KUT_ROL_HIZMET:    return TORE_PROFILE_HIZMET;
        case KUT_ROL_KULLANICI: return TORE_PROFILE_KULLANICI;
        default:                return TORE_PROFILE_KISITLI;
    }
}

uint32_t kut_add_user(const char *name, kut_rol_t rol) {
    for (int i = 0; i < KUT_MAX_USERS; i++) {
        if (users[i].used) continue;
        users[i].used = 1;
        users[i].uid = next_uid++;
        users[i].rol = rol;
        int j = 0;
        while (name[j] && j < KUT_NAME_LEN - 1) { users[i].name[j] = name[j]; j++; }
        users[i].name[j] = 0;

        serial_write("KUT: kullanici tanimlandi '");
        serial_write(users[i].name);
        serial_write("' rol=");
        serial_write(kut_rol_name(rol));
        serial_write(" uid=");
        serial_write_hex(users[i].uid);
        serial_write("\n");
        return users[i].uid;
    }
    log_write(LOG_HATA, "KUT", "kullanici tablosu dolu");
    return 0;
}

const kut_user_t *kut_find_user(uint32_t uid) {
    for (int i = 0; i < KUT_MAX_USERS; i++)
        if (users[i].used && users[i].uid == uid) return &users[i];
    return 0;
}

int kut_bind_process(uint32_t pid, uint32_t uid) {
    const kut_user_t *u = kut_find_user(uid);
    if (!u) return -1;

    int slot = -1;
    for (int i = 0; i < KUT_MAX_BINDINGS; i++) {
        if (bindings[i].used && bindings[i].pid == pid) { slot = i; break; }
        if (!bindings[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return -1;

    bindings[slot].used = 1;
    bindings[slot].pid = pid;
    bindings[slot].uid = uid;

    /* Yetkileri ROLDEN turet ve politika motoruna yaz - boylece
     * yetki karari tek bir yerden (rol) yonetilir. */
    tore_set_capabilities(pid, kut_capabilities_for_role(u->rol));

    serial_write("KUT: pid=");
    serial_write_hex(pid);
    serial_write(" -> kullanici '");
    serial_write(u->name);
    serial_write("' (");
    serial_write(kut_rol_name(u->rol));
    serial_write(") olarak baglandi\n");
    return 0;
}

const kut_user_t *kut_owner_of(uint32_t pid) {
    for (int i = 0; i < KUT_MAX_BINDINGS; i++)
        if (bindings[i].used && bindings[i].pid == pid)
            return kut_find_user(bindings[i].uid);
    return 0;
}
