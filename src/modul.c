/* 1.4.9: önyükleme modülleri (bkz. include/modul.h) */
#include "modul.h"
#include "serial.h"

static onyukleme_modul_t mod[MODUL_AZAMI];
static int mod_n;

void modul_ekle(uint64_t bas, uint64_t son, const char *ad) {
    if (mod_n >= MODUL_AZAMI || son <= bas) return;
    onyukleme_modul_t *m = &mod[mod_n++];
    m->bas = bas; m->son = son;
    int i = 0;
    for (; ad && ad[i] && i < (int)sizeof m->ad - 1; i++) m->ad[i] = ad[i];
    m->ad[i] = 0;
    serial_write("modul: '"); serial_write(m->ad); serial_write("' fiz=");
    serial_write_hex((uint32_t)bas); serial_write(" boy=");
    serial_write_hex((uint32_t)(son - bas)); serial_write("\n");
}

#define MULTIBOOT_INFO_MODS 0x0008
typedef struct { uint32_t bas, son, ad, ayrilmis; } mb1_modul_t;

void modul_mb1_oku(const multiboot_info_t *mbi) {
    if (!mbi || !(mbi->flags & MULTIBOOT_INFO_MODS)) return;
    const mb1_modul_t *m = (const mb1_modul_t *)(uintptr_t)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count && i < MODUL_AZAMI; i++)
        modul_ekle(m[i].bas, m[i].son, m[i].ad ? (const char *)(uintptr_t)m[i].ad : "");
}

int modul_sayisi(void) { return mod_n; }
const onyukleme_modul_t *modul_al(int i) { return (i >= 0 && i < mod_n) ? &mod[i] : 0; }

static int icerir(const char *s, const char *p) {
    for (; *s; s++) { const char *a = s, *b = p; while (*b && *a == *b) { a++; b++; } if (!*b) return 1; }
    return 0;
}
int modul_bul(const char *ad, const uint8_t **veri, uint32_t *boy) {
    for (int i = 0; i < mod_n; i++)
        if (icerir(mod[i].ad, ad)) {
            *veri = (const uint8_t *)(uintptr_t)mod[i].bas;
            *boy = (uint32_t)(mod[i].son - mod[i].bas);
            return 0;
        }
    return -1;
}
