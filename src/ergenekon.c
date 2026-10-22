/* =====================================================================
 * 1.4.9 — ERGENEKON: dizge imajından yeniden kurulum (bkz. ergenekon.h)
 * ===================================================================== */
#include "ergenekon.h"
#include "modul.h"
#include "kervan2.h"
#include "fat16.h"
#include "fat16_ic.h"
#include "disk_cache.h"
#include "depo_kilit.h"
#include "vfs.h"
#include "io.h"
#include "serial.h"
#include "log.h"
#include "bearssl.h"

static const uint8_t *imaj;
static uint32_t imaj_boy;
static int durum_hesap;          /* 0 bakılmadı, 1 geçerli, -1 geçersiz/yok */
static char imaj_neden[64];
static uint32_t son_yazilan, son_ayni, son_hata;

static void kopya(char *d, int boy, const char *s) { int i = 0; while (i < boy - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static int esit(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* imaj her işlemde YENİDEN doğrulanır (bellekteki kopya bozulmuş olabilir) */
static int imaj_hazir(void) {
    if (!imaj && modul_bul("ergenekon", &imaj, &imaj_boy) != 0) {
        imaj = 0;
        kopya(imaj_neden, sizeof imaj_neden, "dizge imaji yok (GRUB modulu yuklenmedi)");
        durum_hesap = -1;
        return -1;
    }
    char n[64];
    if (kervan2_imaj_dogrula(imaj, imaj_boy, n) != 0) {
        kopya(imaj_neden, sizeof imaj_neden, n);
        durum_hesap = -1;
        return -1;
    }
    kopya(imaj_neden, sizeof imaj_neden, "imza ve dosya ozetleri dogrulandi");
    durum_hesap = 1;
    return 0;
}

int ergenekon_durum(ergenekon_durum_t *d) {
    for (unsigned i = 0; i < sizeof *d; i++) ((uint8_t *)d)[i] = 0;
    int r = imaj_hazir();
    d->var = imaj != 0;
    d->gecerli = r == 0;
    kopya(d->neden, sizeof d->neden, imaj_neden);
    if (imaj && r == 0) {
        uint32_t girdi = 0, anahtar = 0;
        kervan2_baslik(imaj, d->ad, d->surum, &girdi, &anahtar);
        d->dosya = girdi; d->anahtar_no = (uint8_t)anahtar;
    }
    d->boy = imaj_boy;
    d->yazilan = son_yazilan; d->ayni = son_ayni; d->hata = son_hata;
    return 0;
}

/* diskteki dosyanın SHA-256'sı; yoksa -1 */
static int disk_ozet(const char *ad, uint8_t o[32], uint32_t *boy) {
    char yol[16]; yol[0] = '/'; kopya(yol + 1, sizeof yol - 1, ad);
    int fd = vfs_open(yol);
    if (fd < 0) return -1;
    static uint8_t t[4096];
    br_sha256_context h; br_sha256_init(&h);
    uint32_t n = 0;
    for (;;) { int r = vfs_read(fd, t, sizeof t); if (r <= 0) break; br_sha256_update(&h, t, (size_t)r); n += (uint32_t)r; }
    vfs_close(fd);
    br_sha256_out(&h, o);
    *boy = n;
    return 0;
}

static ergenekon_dosya_t liste[KERVAN2_IMAJ_AZAMI_DOSYA];
static int liste_n;

int ergenekon_dosya(int sira, ergenekon_dosya_t *o) {
    if (sira == 0) {
        ozkilit_al(&depo_kilit);
        liste_n = 0;
        if (imaj_hazir() == 0) {
            uint32_t girdi = 0;
            kervan2_baslik(imaj, 0, 0, &girdi, 0);
            for (uint32_t i = 0; i < girdi; i++) {
                ergenekon_dosya_t *e = &liste[liste_n++];
                const uint8_t *sha; uint32_t boy;
                kervan2_girdi(imaj, i, e->ad, 0, &boy, &sha);
                e->boy = boy; e->b = 0;
                uint8_t oz[32]; uint32_t db = 0;
                if (disk_ozet(e->ad, oz, &db) != 0) { e->durum = 2; e->disk_boy = 0; continue; }
                e->disk_boy = db;
                e->durum = 0;
                for (int k = 0; k < 32; k++) if (oz[k] != sha[k]) { e->durum = 1; break; }
            }
        }
        ozkilit_birak(&depo_kilit);
    }
    if (sira < 0 || sira >= liste_n) return -1;
    *o = liste[sira];
    return 0;
}

int ergenekon_imaj_dosyasi_mi(const char *ad) {
    if (!imaj || durum_hesap != 1) return 0;
    uint32_t girdi = 0;
    kervan2_baslik(imaj, 0, 0, &girdi, 0);
    char a[13];
    for (uint32_t i = 0; i < girdi; i++) { kervan2_girdi(imaj, i, a, 0, 0, 0); if (esit(a, ad)) return 1; }
    return 0;
}

/* imajdaki i. dosyayı diske yaz, sonra geri okuyup özetini doğrula */
static int yaz_dogrula(uint32_t i) {
    char ad[13]; const uint8_t *v, *sha; uint32_t boy;
    kervan2_girdi(imaj, i, ad, &v, &boy, &sha);
    if (fat16_write_file(ad, v, boy) != (int)boy) return -1;
    uint8_t oz[32]; uint32_t db;
    if (disk_ozet(ad, oz, &db) != 0 || db != boy) return -1;
    for (int k = 0; k < 32; k++) if (oz[k] != sha[k]) return -1;
    return 0;
}

int ergenekon_dosya_kurtar(const char *ad, char *neden) {
    ozkilit_al(&depo_kilit);
    int r = -1;
    if (imaj_hazir() != 0) { kopya(neden, 64, imaj_neden); goto son; }
    uint32_t girdi = 0;
    kervan2_baslik(imaj, 0, 0, &girdi, 0);
    for (uint32_t i = 0; i < girdi; i++) {
        char a[13];
        kervan2_girdi(imaj, i, a, 0, 0, 0);
        if (!esit(a, ad)) continue;
        r = yaz_dogrula(i);
        kopya(neden, 64, r == 0 ? "imajdan geri yuklendi" : "yazilamadi ya da dogrulanamadi");
        if (r == 0) { serial_write("ERGENEKON: dosya imajdan kurtarildi '"); serial_write(ad); serial_write("'\n"); }
        goto son;
    }
    kopya(neden, 64, "imajda boyle bir dosya yok");
son:
    ozkilit_birak(&depo_kilit);
    return r;
}

int ergenekon_yenile(ergenekon_durum_t *d) {
    ozkilit_al(&depo_kilit);
    son_yazilan = son_ayni = son_hata = 0;
    int r = -1;
    if (imaj_hazir() == 0) {
        uint32_t girdi = 0;
        kervan2_baslik(imaj, 0, 0, &girdi, 0);
        for (uint32_t i = 0; i < girdi; i++) {
            char ad[13]; const uint8_t *sha; uint32_t boy;
            kervan2_girdi(imaj, i, ad, 0, &boy, &sha);
            uint8_t oz[32]; uint32_t db;
            int ayni = disk_ozet(ad, oz, &db) == 0 && db == boy;
            for (int k = 0; ayni && k < 32; k++) if (oz[k] != sha[k]) ayni = 0;
            if (ayni) { son_ayni++; continue; }
            if (yaz_dogrula(i) == 0) son_yazilan++; else son_hata++;
        }
        r = son_hata ? -1 : 0;
        serial_write("ERGENEKON: yenile yazilan="); serial_write_hex(son_yazilan);
        serial_write(" ayni="); serial_write_hex(son_ayni);
        serial_write(" hata="); serial_write_hex(son_hata); serial_write("\n");
        log_write(LOG_UYARI, "ERGENEKON", "dizge dosyalari imajdan yenilendi");
    }
    ozkilit_birak(&depo_kilit);
    if (d) ergenekon_durum(d);
    return r;
}

/* birimi biçimle: FAT'lar ve kök dizin boşaltılır, birim adı korunur */
static int bicimle(void) {
    fat16_geo_t g;
    if (fat16_geometri(&g) != 0) return -1;
    static uint8_t s[512], etiket[32];
    int etiket_var = 0;
    if (disk_cache_read_sectors(0, 0, 1, s) != 0) return -1;
    uint8_t ortam = s[21];
    for (uint32_t k = 0; k < g.kok_sektor && !etiket_var; k++) {
        if (disk_cache_read_sectors(0, g.kok_lba + k, 1, s) != 0) return -1;
        for (int e = 0; e < 16; e++) {
            uint8_t *d = s + e * 32;
            if (d[0] == 0) break;
            if (d[0] != 0xE5 && d[11] != 0x0F && (d[11] & 0x08)) {
                for (int i = 0; i < 32; i++) etiket[i] = d[i];
                etiket_var = 1; break;
            }
        }
    }
    for (uint32_t f = 0; f < g.fat_sayisi; f++)
        for (uint32_t k = 0; k < g.fat_sektor; k++) {
            for (int i = 0; i < 512; i++) s[i] = 0;
            if (k == 0) { s[0] = ortam; s[1] = 0xFF; s[2] = 0xFF; s[3] = 0xFF; }
            if (disk_cache_write_sectors(0, g.fat_lba + f * g.fat_sektor + k, 1, s) != 0) return -1;
        }
    for (uint32_t k = 0; k < g.kok_sektor; k++) {
        for (int i = 0; i < 512; i++) s[i] = 0;
        if (k == 0 && etiket_var) for (int i = 0; i < 32; i++) s[i] = etiket[i];
        if (disk_cache_write_sectors(0, g.kok_lba + k, 1, s) != 0) return -1;
    }
    fat16_ipucu_sifirla();
    return 0;
}

int ergenekon_sifirdan(ergenekon_durum_t *d) {
    ozkilit_al(&depo_kilit);
    son_yazilan = son_ayni = son_hata = 0;
    int r = -1;
    if (imaj_hazir() == 0) {
        serial_write("ERGENEKON: birim bicimleniyor (butun dosyalar silinir)\n");
        if (bicimle() != 0) { son_hata = 1; kopya(imaj_neden, sizeof imaj_neden, "bicimleme yazilamadi"); goto son; }
        kervan2_kaydi_unut();
        uint32_t girdi = 0;
        kervan2_baslik(imaj, 0, 0, &girdi, 0);
        for (uint32_t i = 0; i < girdi; i++) {
            if (yaz_dogrula(i) == 0) son_yazilan++; else son_hata++;
        }
        r = son_hata ? -1 : 0;
        serial_write("ERGENEKON: sifirdan kurulum yazilan="); serial_write_hex(son_yazilan);
        serial_write(" hata="); serial_write_hex(son_hata); serial_write("\n");
        log_write(LOG_UYARI, "ERGENEKON", "birim bicimlendi, dizge imajdan kuruldu");
    }
son:
    ozkilit_birak(&depo_kilit);
    if (d) ergenekon_durum(d);
    return r;
}

void ergenekon_yeniden_baslat(void) {
    serial_write("ERGENEKON: yeniden baslatiliyor\n");
    __asm__ volatile ("cli");
    /* 8042: giriş arabelleği boşalınca sıfırlama darbesi */
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) { }
    outb(0x64, 0xFE);
    for (volatile int i = 0; i < 10000000; i++) { }
    /* olmadıysa üçlü hata: boş IDT ile kesme */
    struct __attribute__((packed)) { uint16_t boy; uint64_t taban; } bos = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(bos));
    for (;;) __asm__ volatile ("hlt");
}
