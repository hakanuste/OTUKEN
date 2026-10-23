/* =====================================================================
 * 1.4.9 — ASENA-TEST / ERGENEKON-TEST (yalniz TANI; diskte ASENA.SNM varsa)
 * Disk tests/asena/hazirla.py ile BILINEN bozukluklarla hazirlanir;
 * beklenenler ASENA.SNM'de (dosyalarin gercek zincirlerinden hesaplandi).
 *
 * MOD 1 (ASENA):
 *     1 denetim her bozukluk turunu BEKLENEN sayida buldu (kayip kume ve
 *       zincir fazlasi degerleri dahil)
 *     2 denetim diske yazmadi (iki denetim ayni; bos kume sayisi degismedi)
 *     4 arayuz: Uygulamalar -> ASENA, ozet satiri OCR ile "N sorun bulundu:"
 *     8 arayuz: Onar iki kez -> durum satiri "N sorun onarıldı,"
 *    16 onarim sonrasi denetim TEMIZ
 *   (ana makinede fsck.fat -n de temiz olmali: tests/asena/calistir.sh)
 * MOD 2 (ERGENEKON, MOD 1'in onardigi disk):
 *     1 imaj dogrulandi; imajdan farkli dizge dosyalari = ASENA'nin kestikleri
 *     2 arayuz: ASENA "Dizge dosyalari" -> DEDE.ELF sec -> "Imajdan kurtar"
 *       -> satir "aynı" (OCR)
 *     4 arayuz: ERGENEKON "Dizgeyi yenile" iki kez -> kalan farklilar yazildi,
 *       kullanici dosyasi ve kurulu paket KALDI
 *     8 arayuz: "Sifirdan kur" + EVET -> kokte YALNIZ imaj dosyalari (ayni
 *       ozet), kullanici dosyasi ve paket YOK, KERVAN kaydi bos, bos kume
 *       sayisi imajdan hesaplananla ayni, ASENA denetimi temiz
 *    16 (UYG-TEST bitince) arayuz: "Yeniden baslat" iki kez -> makine
 *       yeniden baslar (QEMU -no-reboot ile cikar; calistir.sh denetler)
 * ===================================================================== */
#include "sinama_asena.h"
#include "sinama_dizin.h"
#include "sinama148.h"
#include "sinama149.h"
#include "sinama_uyg.h"
#include "asena.h"
#include "ergenekon.h"
#include "kervan2.h"
#include "uygulama.h"
#include "fat16.h"
#include "vfs.h"
#include "gui.h"
#include "ulgen.h"
#include "ulgen_theme.h"
#include "keyboard.h"
#include "thread.h"
#include "timer.h"
#include "serial.h"

#define BEKLE(kosul, tik) do { uint64_t _t0 = timer_get_ticks(); \
    while (!(kosul) && timer_get_ticks() - _t0 < (tik)) thread_sleep(1); } while (0)

#define R_ARKA    0x14232Bu
#define R_PANO    0x1B313Au
#define R_YAZI    0xE8E2D0u
#define R_VURGU   0x4FB3A9u
#define R_UYARI   0xE0A060u
#define R_KIRMIZI 0xD9534Fu

static volatile int s_mod, s_ana_bitti;
int sinama_asena_mod(void) { return __atomic_load_n(&s_mod, __ATOMIC_ACQUIRE); }
int sinama_asena_bitti(void) { return __atomic_load_n(&s_ana_bitti, __ATOMIC_ACQUIRE); }

static const char *const TUR[ASENA_TUR_SAYISI] = {
    "FAT_FARK", "KAYIP", "CAPRAZ", "KIRIK", "DONGU", "BOY_FAZLA", "ZINCIR_FAZLA", "GECERSIZ_BAS"
};

static int esit(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int onek(const char *s, const char *o) { while (*o) { if (*s != *o) return 0; s++; o++; } return 1; }
static void yaz_sayi(uint64_t v) {
    char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char c[24]; int k = 0; while (n) c[k++] = t[--n]; c[k] = 0; serial_write(c);
}
static void ek(char *d, int boy, const char *k) { int n = 0; while (d[n]) n++; while (*k && n < boy - 1) d[n++] = *k++; d[n] = 0; }
static void ek_sayi(char *d, int boy, uint64_t v) {
    char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char c[24]; int k = 0; while (n) c[k++] = t[--n]; c[k] = 0; ek(d, boy, c);
}
static void t_tikla(int x, int y) { gui_fare_isle(x, y, 1); thread_sleep(1); gui_fare_isle(x, y, 0); }
static int dosya_var(const char *ad) {
    char yol[16]; yol[0] = '/'; int i = 0; while (ad[i] && i < 13) { yol[i + 1] = ad[i]; i++; } yol[i + 1] = 0;
    int fd = vfs_open(yol); if (fd < 0) return 0; vfs_close(fd); return 1;
}

/* ASENA.SNM */
static char snm[1024];
static uint32_t bek[ASENA_TUR_SAYISI], kayip_deger, zfazla_deger;
static char bozuk[8][13]; static int bozuk_n;
static uint32_t sayi_oku(const char **p) { uint32_t v = 0; while (**p == ' ') (*p)++; while (**p >= '0' && **p <= '9') v = v * 10 + (uint32_t)(*(*p)++ - '0'); return v; }
static int snm_oku(void) {
    bozuk_n = 0; kayip_deger = zfazla_deger = 0;
    for (int t = 0; t < ASENA_TUR_SAYISI; t++) bek[t] = 0;
    int fd = vfs_open("/ASENA.SNM");
    if (fd < 0) return -1;
    int n = vfs_read(fd, snm, sizeof snm - 1);
    vfs_close(fd);
    if (n <= 0) return -1;
    snm[n] = 0;
    const char *p = snm;
    while (*p) {
        if (onek(p, "MOD ")) { p += 4; s_mod = (int)sayi_oku(&p); }
        else if (onek(p, "BEK ")) {
            p += 4;
            for (int t = 0; t < ASENA_TUR_SAYISI; t++) {
                int l = 0; while (TUR[t][l]) l++;
                if (onek(p, TUR[t]) && p[l] == ' ') { p += l; bek[t] = sayi_oku(&p); break; }
            }
        } else if (onek(p, "KAYIP_DEGER ")) { p += 12; kayip_deger = sayi_oku(&p); }
        else if (onek(p, "ZINCIR_FAZLA_DEGER ")) { p += 19; zfazla_deger = sayi_oku(&p); }
        else if (onek(p, "BOZUK ") && bozuk_n < 8) {
            p += 6; int i = 0; while (*p && *p != '\n' && i < 12) bozuk[bozuk_n][i++] = *p++; bozuk[bozuk_n++][i] = 0;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

/* Uygulamalar menüsünden aç, pencere kimliği */
static int menuden_ac(int tamga, int u, uint32_t hz) {
    int ux, uy, uw, uh;
    ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
    t_tikla(ux + 30, ULGEN_UST_BAR / 2);
    thread_sleep(2);
    if (!ulgen_theme_uyg_menu_acik()) return -1;
    t_tikla(ux + 40, uy + 4 + tamga * 26 + 13);
    BEKLE(uygulama_penceresi(u) >= 0, 10 * hz);
    return uygulama_penceresi(u);
}
static void kapat(int id, int u, uint32_t hz) {
    const ulgen_yuzey_t *ys = ulgen_yuzey_al(id);
    if (ys) {
        int kx = ys->x + ys->w - 10, ky = ys->y - ULGEN_BASLIK_YUK / 2;
        if (ulgen_kapat_dugmesi_mi(id, kx, ky)) t_tikla(kx, ky);
    }
    BEKLE(uygulama_pid(u) == 0, 10 * hz);
}
static void ic_tikla(int id, int x, int y) {
    const ulgen_yuzey_t *ys = ulgen_yuzey_al(id);
    if (ys) t_tikla(ys->x + x, ys->y + y);
}

static void rapor_yaz(const asena_rapor_t *r) {
    for (int t = 0; t < ASENA_TUR_SAYISI; t++) { serial_write(" "); serial_write(TUR[t]); serial_write("="); yaz_sayi(r->sayi[t]); }
    serial_write(" toplam="); yaz_sayi(r->bulgu);
}

static void mod1(uint32_t hz) {
    int hata = 0;
    asena_rapor_t r1, r2;
    sinama_umay_gunluk_durgun(60);      /* UMAY'in acilis yazimlari olcumu bozmasin */
    uint32_t bos0 = fat16_bos_kume_sayisi();
    asena_tara(0, &r1);
    serial_write("ASENA-TEST: denetim:"); rapor_yaz(&r1); serial_write("\n");
    for (int t = 0; t < ASENA_TUR_SAYISI; t++) if (r1.sayi[t] != bek[t]) hata |= 1;
    for (int i = 0; ; i++) {
        asena_bulgu_t b;
        if (asena_bulgu(i, &b) != 0) break;
        serial_write("ASENA-TEST:   "); serial_write(asena_tur_adi(b.tur)); serial_write(" '"); serial_write(b.yol);
        serial_write("' "); yaz_sayi(b.deger); serial_write("\n");
        if (b.tur == ASENA_KAYIP && b.deger != kayip_deger) hata |= 1;
        if (b.tur == ASENA_ZINCIR_FAZLA && b.deger != zfazla_deger) hata |= 1;
    }
    asena_tara(0, &r2);
    for (int t = 0; t < ASENA_TUR_SAYISI; t++) if (r1.sayi[t] != r2.sayi[t]) hata |= 2;
    if (fat16_bos_kume_sayisi() != bos0) hata |= 2;

    /* arayüz */
    int id = menuden_ac(7, UYG_ASENA, hz);
    char s[160], bek_s[64];
    if (id < 0) hata |= 4;
    else {
        bek_s[0] = 0; ek_sayi(bek_s, sizeof bek_s, r1.bulgu); ek(bek_s, sizeof bek_s, " sorun bulundu:");
        BEKLE((sinama_satir_ocr(id, 16, 96, 40, R_UYARI, R_ARKA, s, sizeof s), esit(s, bek_s)), 10 * hz);
        serial_write("ASENA-TEST:   ozet (OCR) '"); serial_write(s); serial_write("'\n");
        if (!esit(s, bek_s)) hata |= 4;
        ic_tikla(id, 140, 50);                    /* Onar (1: onay istenir) */
        thread_sleep(hz / 5);
        ic_tikla(id, 140, 50);                    /* Onar (2: yazar) */
        bek_s[0] = 0; ek_sayi(bek_s, sizeof bek_s, r1.bulgu); ek(bek_s, sizeof bek_s, " sorun onarıldı,");
        BEKLE((sinama_satir_ocr(id, 16, 480 - 26, 60, R_VURGU, R_ARKA, s, sizeof s), onek(s, bek_s)), 20 * hz);
        serial_write("ASENA-TEST:   onarim (OCR) '"); serial_write(s); serial_write("'\n");
        if (!onek(s, bek_s)) hata |= 8;
        char temiz[64];
        BEKLE((sinama_satir_ocr(id, 16, 96, 40, R_VURGU, R_ARKA, temiz, sizeof temiz), esit(temiz, "Dosya sistemi sağlam: sorun yok.")), 5 * hz);
        if (!esit(temiz, "Dosya sistemi sağlam: sorun yok.")) hata |= 8;
        kapat(id, UYG_ASENA, hz);
    }
    asena_rapor_t r3;
    asena_tara(0, &r3);
    serial_write("ASENA-TEST: onarim sonrasi:"); rapor_yaz(&r3); serial_write("\n");
    if (r3.bulgu != 0 || r3.hata) hata |= 16;
    serial_write("ASENA-TEST: FAT16 denetim + onarim (bilinen bozukluklar, arayuz) hata=");
    yaz_sayi((uint64_t)hata);
    serial_write(hata ? " -> HATA\n" : " -> BASARILI\n");
}

static int dizge_farkli(char out[][13], int azami) {
    int n = 0; ergenekon_dosya_t d;
    for (int i = 0; ergenekon_dosya(i, &d) == 0; i++) if (d.durum && n < azami) { int k = 0; while (d.ad[k]) { out[n][k] = d.ad[k]; k++; } out[n][k] = 0; n++; }
    return n;
}

static void mod2(uint32_t hz) {
    int hata = 0;
    ergenekon_durum_t ed;
    ergenekon_durum(&ed);
    serial_write("ERGENEKON-TEST: imaj "); serial_write(ed.gecerli ? ed.ad : "GECERSIZ"); serial_write(" dosya=");
    yaz_sayi(ed.dosya); serial_write(" anahtar="); yaz_sayi(ed.anahtar_no); serial_write(" "); serial_write(ed.neden); serial_write("\n");
    if (!ed.gecerli) hata |= 1;
    char fark[32][13];
    int fn = dizge_farkli(fark, 32);
    serial_write("ERGENEKON-TEST:   imajdan farkli:");
    for (int i = 0; i < fn; i++) { serial_write(" "); serial_write(fark[i]); }
    serial_write("\n");
    if (fn != bozuk_n) hata |= 1;
    for (int i = 0; i < bozuk_n; i++) { int var = 0; for (int j = 0; j < fn; j++) if (esit(bozuk[i], fark[j])) var = 1; if (!var) hata |= 1; }

    /* 2) ASENA arayüzünden tek dosya kurtarma: DEDE.ELF */
    int id = menuden_ac(7, UYG_ASENA, hz);
    char s[96];
    if (id < 0) hata |= 2;
    else {
        ic_tikla(id, 260, 50);                          /* Dizge dosyaları */
        thread_sleep(hz / 2);
        int sira = -1; ergenekon_dosya_t d;
        for (int i = 0; ergenekon_dosya(i, &d) == 0; i++)      /* uygulamanın listesiyle aynı sıra */
            if (esit(d.ad, "DEDE.ELF")) { sira = i; break; }
        if (sira < 0) hata |= 2;
        else {
            int y = 96 + sira * 18;
            /* uygulama listeyi okuyup çizmeden tıklanırsa yanlış satır seçilir
             * (smp4'te liste özetleri geç bitebiliyor): önce satır görünsün,
             * tıklamadan sonra seçili (vurgulu zemin) olduğu görülsün */
            uint64_t t_l = timer_get_ticks();
            BEKLE((sinama_satir_ocr(id, 304, y, 6, R_KIRMIZI, R_ARKA, s, sizeof s), esit(s, "FARKLI")), 30 * hz);
            serial_write("ERGENEKON-TEST:   liste gorundu, tik="); serial_write_hex((uint32_t)(timer_get_ticks() - t_l));
            serial_write(" '"); serial_write(s); serial_write("'\n");
            ic_tikla(id, 30, y + 6);                        /* satırı seç */
            BEKLE((sinama_satir_ocr(id, 304, y, 6, R_KIRMIZI, 0x24424Cu, s, sizeof s), esit(s, "FARKLI")), 5 * hz);
            if (!esit(s, "FARKLI")) { hata |= 2; serial_write("ERGENEKON-TEST:   DEDE.ELF satiri secilemedi\n"); }
            ic_tikla(id, 400, 50);                          /* İmajdan kurtar */
            BEKLE((sinama_satir_ocr(id, 304, y, 6, R_VURGU, 0x24424Cu, s, sizeof s), esit(s, "aynı")), 10 * hz);
            serial_write("ERGENEKON-TEST:   ASENA kurtar DEDE.ELF satir (OCR) '"); serial_write(s); serial_write("'\n");
            if (!esit(s, "aynı")) hata |= 2;
        }
        kapat(id, UYG_ASENA, hz);
    }

    /* 3) kullanıcı durumu: dosya + paket; sonra "Dizgeyi yenile" */
    static const char KULLANICI[] = "Hakan'in notu: bu dosya yenilemede kalmali.\n";
    char neden[64];
    if (fat16_write_file("KULLANI.TXT", (const uint8_t *)KULLANICI, sizeof KULLANICI - 1) < 0) hata |= 4;
    if (kervan2_kur("ORNEK.KRV", neden) != 0) { hata |= 4; serial_write("ERGENEKON-TEST:   paket kurulamadi: "); serial_write(neden); serial_write("\n"); }
    fn = dizge_farkli(fark, 32);
    uint32_t kalan = (uint32_t)fn;
    id = menuden_ac(8, UYG_ERGENEKON, hz);
    if (id < 0) hata |= 4;
    else {
        ic_tikla(id, 60, 118); thread_sleep(hz / 5); ic_tikla(id, 60, 118);   /* Dizgeyi yenile x2 */
        char bek_s[96]; bek_s[0] = 0;
        ek(bek_s, sizeof bek_s, "Yenileme: tamam, "); ek_sayi(bek_s, sizeof bek_s, kalan);
        ek(bek_s, sizeof bek_s, " dosya yazıldı, "); ek_sayi(bek_s, sizeof bek_s, ed.dosya - kalan);
        ek(bek_s, sizeof bek_s, " zaten sağlam.");
        BEKLE((sinama_satir_ocr(id, 16, 420 - 26, 70, R_VURGU, R_ARKA, s, sizeof s), esit(s, bek_s)), 30 * hz);
        serial_write("ERGENEKON-TEST:   yenile (OCR) '"); serial_write(s); serial_write("'\n");
        if (!esit(s, bek_s)) hata |= 4;
        if (dizge_farkli(fark, 32) != 0) hata |= 4;
        if (!dosya_var("KULLANI.TXT") || !kervan2_kurulu_mu("ornek-belgeler", 0)) hata |= 4;

        /* 4) Sıfırdan kur: EVET */
        ic_tikla(id, 60, 158);
        thread_sleep(hz / 5);
        const char *e = "evet\r";
        for (; *e; e++) keyboard_push_char(*e);
        bek_s[0] = 0;
        ek(bek_s, sizeof bek_s, "Sıfırdan kurulum: tamam, "); ek_sayi(bek_s, sizeof bek_s, ed.dosya);
        ek(bek_s, sizeof bek_s, " dosya yazıldı.");
        BEKLE((sinama_satir_ocr(id, 16, 420 - 26, 70, R_VURGU, R_ARKA, s, sizeof s), esit(s, bek_s)), 60 * hz);
        serial_write("ERGENEKON-TEST:   sifirdan (OCR) '"); serial_write(s); serial_write("'\n");
        if (!esit(s, bek_s)) hata |= 8;
        kapat(id, UYG_ERGENEKON, hz);            /* UYG-TEST pencereyi kendisi acacak */
    }
    /* kök dizin = imaj dosyaları, tam olarak */
    fat16_dirent_t kok[64];
    int kn = fat16_list_root(kok, 64);
    int eslesen = 0; uint32_t kume = 0;
    for (int i = 0; i < kn; i++) {
        if (!ergenekon_imaj_dosyasi_mi(kok[i].name)) { hata |= 8; serial_write("ERGENEKON-TEST:   fazla dosya: "); serial_write(kok[i].name); serial_write("\n"); }
        else eslesen++;
        kume += (kok[i].size + 511) / 512;
    }
    if (eslesen != (int)ed.dosya) hata |= 8;
    if (dizge_farkli(fark, 32) != 0) hata |= 8;
    if (dosya_var("KULLANI.TXT") || dosya_var("KERVAN.LST") || kervan2_kurulu_sayisi() != 0) hata |= 8;
    asena_rapor_t r;
    asena_tara(0, &r);
    if (r.bulgu != 0 || r.bos_kume + kume != r.kume_sayisi) hata |= 8;
    serial_write("ERGENEKON-TEST:   kok="); yaz_sayi((uint64_t)kn); serial_write(" imaj="); yaz_sayi(ed.dosya);
    serial_write(" bos="); yaz_sayi(r.bos_kume); serial_write(" dolu="); yaz_sayi(kume); serial_write(" toplam="); yaz_sayi(r.kume_sayisi); serial_write("\n");
    serial_write("ERGENEKON-TEST: imaj dogrulama, ASENA kurtar, yenile, sifirdan kur (arayuz) hata=");
    yaz_sayi((uint64_t)hata);
    serial_write(hata ? " -> HATA\n" : " -> BASARILI\n");

    /* 5) UYG-TEST (yeni kurulan diskten uygulamalar) bitince: yeniden başlat */
    __atomic_store_n(&s_ana_bitti, 1, __ATOMIC_RELEASE);
    BEKLE(sinama_uyg_bitti(), 120 * hz);
    thread_sleep(hz);
    id = menuden_ac(8, UYG_ERGENEKON, hz);
    if (id >= 0) {
        serial_write("ERGENEKON-TEST: yeniden baslatma deneniyor\n");
        ic_tikla(id, 60, 198); thread_sleep(hz / 5); ic_tikla(id, 60, 198);
        thread_sleep(5 * hz);
        serial_write("ERGENEKON-TEST: yeniden baslatma OLMADI -> HATA\n");
    }
}

static void gorev(void) {
    if (snm_oku() != 0 || (s_mod != 1 && s_mod != 2)) {
        serial_write("ASENA-TEST: atlandi (sinama diskinde ASENA.SNM yok; bkz. tests/asena)\n");
        __atomic_store_n(&s_ana_bitti, 1, __ATOMIC_RELEASE);
        thread_exit();
    }
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    BEKLE(sinama148_bitti(), 120 * hz);
    thread_sleep(hz / 2);
    if (s_mod == 1) { mod1(hz); __atomic_store_n(&s_ana_bitti, 1, __ATOMIC_RELEASE); }
    else mod2(hz);
    thread_exit();
}

/* mod bilgisi ASENA.SNM okununca belli olur: UYG-TEST ondan önce sormamalı */
void sinama_asena_baslat(void) {
    if (snm_oku() != 0) s_mod = 0;
    thread_create("AsenaSinama", gorev);
}
