#include "tore.h"
#include "serial.h"

#define TORE_MAX_ENTRIES 16

typedef struct {
    int used;
    uint32_t pid;
    uint32_t capabilities;
} tore_entry_t;

static tore_entry_t table[TORE_MAX_ENTRIES];
static uint32_t denied_count = 0;

void tore_init(void) {
    for (int i = 0; i < TORE_MAX_ENTRIES; i++) table[i].used = 0;
    denied_count = 0;
    serial_write("TORE: guvenlik politikasi motoru baslatildi\n");
}

static tore_entry_t *find_entry(uint32_t pid) {
    for (int i = 0; i < TORE_MAX_ENTRIES; i++)
        if (table[i].used && table[i].pid == pid) return &table[i];
    return 0;
}

int tore_set_capabilities(uint32_t pid, uint32_t capabilities) {
    tore_entry_t *e = find_entry(pid);
    if (!e) {
        for (int i = 0; i < TORE_MAX_ENTRIES; i++) {
            if (!table[i].used) { e = &table[i]; e->used = 1; e->pid = pid; break; }
        }
    }
    if (!e) {
        serial_write("TORE: HATA - politika tablosu dolu\n");
        return -1;
    }
    e->capabilities = capabilities;

    serial_write("TORE: pid=");
    serial_write_hex(pid);
    serial_write(" yetkileri ayarlandi=");
    serial_write_hex(capabilities);
    serial_write("\n");
    return 0;
}

uint32_t tore_get_capabilities(uint32_t pid) {
    tore_entry_t *e = find_entry(pid);
    return e ? e->capabilities : 0;
}

int tore_check(uint32_t pid, kut_capability_t cap, const char *action) {
    tore_entry_t *e = find_entry(pid);
    uint32_t caps = e ? e->capabilities : 0;

    if (caps & (uint32_t)cap) {
        return 1; /* izinli */
    }

    denied_count++;
    serial_write("TORE: ERISIM REDDEDILDI pid=");
    serial_write_hex(pid);
    serial_write(" istenen_yetki=");
    serial_write(tore_capability_name(cap));
    serial_write(" islem='");
    serial_write(action);
    serial_write("'\n");
    return 0;
}

uint32_t tore_denied_count(void) { return denied_count; }

const char *tore_capability_name(kut_capability_t cap) {
    switch (cap) {
        case KUT_DISK_READ:   return "DISK_OKU";
        case KUT_DISK_WRITE:  return "DISK_YAZ";
        case KUT_NET_SEND:    return "AG_GONDER";
        case KUT_NET_RECV:    return "AG_AL";
        case KUT_MEM_ALLOC:   return "BELLEK_AYIR";
        case KUT_PROC_CREATE: return "SUREC_OLUSTUR";
        case KUT_PROC_KILL:   return "SUREC_SONLANDIR";
        case KUT_HW_DIRECT:   return "DONANIM_DOGRUDAN";
        default:              return "BILINMEYEN";
    }
}
