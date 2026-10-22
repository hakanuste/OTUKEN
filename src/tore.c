#include "tore.h"
#include "serial.h"

/* SMP adim 6: yetki tablosu. Kisa islemler -> ozyinelemeli donen kilit. */
#include "spinlock.h"
static ozspin_t tore_kilit = OZSPIN_BASLANGIC;

#define TORE_MAX_ENTRIES 16

typedef struct {
    int used;
    uint32_t pid;
    uint32_t capabilities;
    char yazma_kok[32];   /* 1.4.9: bos degilse DISK_WRITE yalniz bu dizin altinda */
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

static int tore_set_capabilities__ic(uint32_t pid, uint32_t capabilities) {
    tore_entry_t *e = find_entry(pid);
    if (!e) {
        for (int i = 0; i < TORE_MAX_ENTRIES; i++) {
            if (!table[i].used) { e = &table[i]; e->used = 1; e->pid = pid; e->yazma_kok[0] = 0; break; }
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

static uint32_t tore_get_capabilities__ic(uint32_t pid) {
    tore_entry_t *e = find_entry(pid);
    return e ? e->capabilities : 0;
}

static int tore_check__ic(uint32_t pid, kut_capability_t cap, const char *action) {
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

/* ===== 1.4.9: yazma koku (DISK_WRITE'i bir dizin agacina daraltir) ===== */
static char buyuk(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
/* a, b'nin kendisi ya da altindaki bir yol mu (buyuk/kucuk harf duyarsiz) */
static int altinda(const char *yol, const char *kok) {
    int i = 0;
    while (kok[i]) { if (buyuk(yol[i]) != buyuk(kok[i])) return 0; i++; }
    return yol[i] == 0 || yol[i] == '/';
}
int tore_yazma_koku(uint32_t pid, const char *kok) {
    ozspin_al(&tore_kilit);
    tore_entry_t *e = find_entry(pid);
    int r = -1;
    if (e) {
        int i = 0;
        for (; kok && kok[i] && i < (int)sizeof e->yazma_kok - 1; i++) e->yazma_kok[i] = kok[i];
        e->yazma_kok[i] = 0;
        r = 0;
    }
    ozspin_birak(&tore_kilit);
    return r;
}
int tore_yazma_koku_al(uint32_t pid, char *cikti, int boy) {
    ozspin_al(&tore_kilit);
    tore_entry_t *e = find_entry(pid);
    int i = 0;
    if (e) for (; e->yazma_kok[i] && i < boy - 1; i++) cikti[i] = e->yazma_kok[i];
    cikti[i] = 0;
    ozspin_birak(&tore_kilit);
    return i;
}
/* dizin_olustur: kokun USTLERI de olusturulabilir (/KUT, sonra /KUT/UMAY) */
int tore_yol_yazilabilir(uint32_t pid, const char *yol, int dizin_olustur) {
    char kok[32];
    if (tore_yazma_koku_al(pid, kok, sizeof kok) == 0) return 1;   /* daraltma yok */
    if (yol[0] == '/' && yol[1] == '.') return 0;
    for (const char *p = yol; *p; p++) if (p[0] == '/' && p[1] == '.' ) return 0;   /* "." ve ".." yok */
    if (altinda(yol, kok)) return 1;
    if (dizin_olustur && altinda(kok, yol)) return 1;
    denied_count++;
    serial_write("TORE: YAZMA KOKU DISI reddedildi pid=");
    serial_write_hex(pid);
    serial_write(" yol='"); serial_write(yol); serial_write("' kok='"); serial_write(kok); serial_write("'\n");
    return 0;
}

/* 1.4.7: olen gorevin yetki kaydini siler (reap'ten). Gorev kimlikleri
 * artan sayac oldugundan eski kayit YENI goreve yetki VEREMEZ; ama
 * silinmezse 16 girislik tablo dolar ve yeni programlar yetkisiz kalir
 * (kapali yone duser). Donen kilit; uyumaz. */
static void tore_gorev_bitti__ic(uint32_t pid) {
    for (int i = 0; i < TORE_MAX_ENTRIES; i++)
        if (table[i].used && table[i].pid == pid) { table[i].used = 0; table[i].capabilities = 0; table[i].yazma_kok[0] = 0; }
}

void tore_gorev_bitti(uint32_t pid) {
    ozspin_al(&tore_kilit);
    tore_gorev_bitti__ic(pid);
    ozspin_birak(&tore_kilit);
}

/* Sinama: pid'nin yetki kaydi var mi (1/0). */
int tore_kayit_var_mi(uint32_t pid) {
    ozspin_al(&tore_kilit);
    int var = find_entry(pid) != 0;
    ozspin_birak(&tore_kilit);
    return var;
}

/* Sinama: dolu yetki kaydi sayisi. */
int tore_kayit_sayisi(void) {
    int n = 0;
    ozspin_al(&tore_kilit);
    for (int i = 0; i < TORE_MAX_ENTRIES; i++) if (table[i].used) n++;
    ozspin_birak(&tore_kilit);
    return n;
}

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
        case KUT_IPC:         return "IPC";
        case KUT_EKRAN:       return "EKRAN";
        case KUT_KLAVYE:      return "KLAVYE";
        case KUT_SES:         return "SES";
        case KUT_SISTEM:      return "SISTEM_BILGI";
        case KUT_AYAR:        return "AYAR";
        case KUT_TANI:        return "TANI";
        case KUT_KERVAN:      return "PAKET_KUR";
        case KUT_KURTARMA:    return "KURTARMA";
        default:              return "BILINMEYEN";
    }
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int tore_set_capabilities(uint32_t pid, uint32_t capabilities) {
    ozspin_al(&tore_kilit);
    int r = tore_set_capabilities__ic(pid, capabilities);
    ozspin_birak(&tore_kilit);
    return r;
}

uint32_t tore_get_capabilities(uint32_t pid) {
    ozspin_al(&tore_kilit);
    uint32_t r = tore_get_capabilities__ic(pid);
    ozspin_birak(&tore_kilit);
    return r;
}

int tore_check(uint32_t pid, kut_capability_t cap, const char *action) {
    ozspin_al(&tore_kilit);
    int r = tore_check__ic(pid, cap, action);
    ozspin_birak(&tore_kilit);
    return r;
}
