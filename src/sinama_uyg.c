/* =====================================================================
 * 1.4.9 — UYG-TEST (yalniz TANI): ULAK, DEDE KORKUT, TORE, TIMUR, KUT
 * Kullanici gibi: ust cubuktaki "Uygulamalar" sozcugune GERCEK tiklama,
 * acilan menude tamga satirina tiklama. Her uygulama icin:
 *     1 menu acildi ve satir tiklamasi uygulamayi baslatti (penceresi var)
 *     2 surecin TORE yetkisi TAM OLARAK profili (fazlasi yok)
 *     4 pencerenin SUNULAN karesinden baslik OCR ile okundu
 *     8 anahtar satir OCR ile okundu ve CEKIRDEGIN kendi verisiyle ayni
 *    16 kapat dugmesi: surec bitti
 *    32 sonuk tamga (ASENA) hicbir sey baslatmadi, menu kapandi
 *    64 TORE "Cagrilar" gorunumu: satir cagri adi + gereken yetki cekirdekle ayni
 * Beklenen degerler sabit yazilmaz: cekirdek islevlerinden (ag_bilgi,
 * ses_var, kut_kullanici_al, EFER/CR4, syscall_politika_adi) uretilir.
 * Renkler ve yerlesim userprog/{ulak,dede,toreg,timur,kut}.c ile ayni.
 * ===================================================================== */
#include "sinama_uyg.h"
#include "sinama148.h"
#include "sinama149.h"
#include "sinama_kervan.h"
#include "uygulama.h"
#include "gui.h"
#include "ulgen.h"
#include "ulgen_theme.h"
#include "thread.h"
#include "timer.h"
#include "serial.h"
#include "tore.h"
#include "kut.h"
#include "ag.h"
#include "ses.h"
#include "syscall.h"
#include "tema.h"
#include "tamga.h"
#include "dede.h"
#include "asena.h"
#include "ergenekon.h"
#include "sinama_asena.h"
#include "bearssl.h"

#define BEKLE(kosul, tik) do { uint64_t _t0 = timer_get_ticks(); \
    while (!(kosul) && timer_get_ticks() - _t0 < (tik)) thread_sleep(1); } while (0)

#define R_ARKA    0x14232Bu
#define R_PANO    0x1B313Au
#define R_YAZI    0xE8E2D0u
#define R_SONUK   0x8FA3A8u
#define R_VURGU   0x4FB3A9u
#define R_UYARI   0xE0A060u

static int esit(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void ek(char *d, int boy, const char *k) { int n = 0; while (d[n]) n++; while (*k && n < boy - 1) d[n++] = *k++; d[n] = 0; }
static void ek_sayi(char *d, int boy, uint64_t v) {
    char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char c[24]; int k = 0; while (n) c[k++] = t[--n]; c[k] = 0; ek(d, boy, c);
}
static void ip_ek(char *d, int boy, const uint8_t ip[4]) {
    for (int i = 0; i < 4; i++) { ek_sayi(d, boy, ip[i]); if (i < 3) ek(d, boy, "."); }
}
static void yaz_sayi(uint64_t v) { char s[24]; s[0] = 0; ek_sayi(s, sizeof s, v); serial_write(s); }
static void t_tikla(int x, int y) { gui_fare_isle(x, y, 1); thread_sleep(1); gui_fare_isle(x, y, 0); }

static uint64_t rdmsr_(uint32_t m) { uint32_t a, d; __asm__ volatile ("rdmsr" : "=a"(a), "=d"(d) : "c"(m)); return ((uint64_t)d << 32) | a; }
static uint64_t cr4_(void) { uint64_t v; __asm__ volatile ("mov %%cr4, %0" : "=r"(v)); return v; }

/* "Uygulamalar" menusunu ac ve tamga satirina tikla */
static int menuden_ac(int tamga) {
    int ux, uy, uw, uh;
    ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
    t_tikla(ux + 30, ULGEN_UST_BAR / 2);
    thread_sleep(2);
    if (!ulgen_theme_uyg_menu_acik()) return -1;
    /* menu EKRANDA: tamga satirinin adi VRAM'dan okunur (sonuklar sonuk renkte) */
    const tema_t *tm = tema_simdiki();
    uint32_t sonuk = ((tm->gorev_yazi >> 1) & 0x7F7F7Fu) + ((tm->gorev_cubugu >> 1) & 0x7F7F7Fu);
    const tamga_t *tg = tamga_al((tamga_id_t)tamga);
    char s[48];
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    /* ekran birlestirmesi bir sonraki kareye kalabilir: en cok 1 sn bekle */
    BEKLE((sinama_ekran_ocr(ux + 14, uy + 4 + tamga * 26 + 5, 14, ulgen_theme_uyg_var(tamga) ? tm->gorev_yazi : sonuk,
                            tm->gorev_cubugu, s, sizeof s), tg && esit(s, tg->ad)), hz);
    serial_write("UYG-TEST:   menu satiri (ekran) '"); serial_write(s); serial_write("'\n");
    if (!tg || !esit(s, tg->ad)) {
        t_tikla(ux + uw + 200, uy + uh + 40);       /* menuyu kapat: sonraki sinama temiz baslasin */
        return -3;
    }
    t_tikla(ux + 40, uy + 4 + tamga * 26 + 13);
    thread_sleep(2);
    return ulgen_theme_uyg_menu_acik() ? -2 : 0;
}

static int yetki_dizisi(uint32_t y, char *s, int boy) {
    s[0] = 0;
    int ilk = 1;
    for (int b = 0; b < KUT_YETKI_SAYISI; b++) if (y & (1u << b)) {
        if (!ilk) ek(s, boy, ",");
        ek(s, boy, tore_capability_name((kut_capability_t)(1u << b)));
        ilk = 0;
    }
    return 0;
}

typedef struct {
    int tamga, uyg; uint32_t profil;
    const char *ad, *baslik;
} hedef_t;

static void kapat(int id, int u, uint32_t hz, int *h) {
    const ulgen_yuzey_t *ys = ulgen_yuzey_al(id);
    if (ys) {
        int kx = ys->x + ys->w - 10, ky = ys->y - ULGEN_BASLIK_YUK / 2;
        if (ulgen_kapat_dugmesi_mi(id, kx, ky)) t_tikla(kx, ky);
    }
    BEKLE(uygulama_pid(u) == 0, 10 * hz);
    if (uygulama_pid(u) != 0) *h |= 16;
}

/* uygulamaya ozgu anahtar satir: OCR ile oku, cekirdek verisiyle karsilastir */
static int anahtar(const hedef_t *t, int id, uint32_t hz, char *oku, int on) {
    char bek[160]; bek[0] = 0;
    int ok = 0;
    switch (t->uyg) {
    case UYG_ULAK: {
        ag_bilgi_t b; ag_bilgi(&b);
        if (!b.yapilandi) { ek(bek, sizeof bek, "bekleniyor…"); }
        else { ip_ek(bek, sizeof bek, b.ip); ek(bek, sizeof bek, " / "); ip_ek(bek, sizeof bek, b.maske); }
        BEKLE((sinama_satir_ocr(id, 170, 82, 40, b.yapilandi ? R_YAZI : R_UYARI, R_ARKA, oku, on), esit(oku, bek)), 3 * hz);
        ok = esit(oku, bek);
        char k[40]; sinama_satir_ocr(id, 16, 42, 14, R_SONUK, R_ARKA, k, sizeof k);
        ok = ok && esit(k, "Kart (e1000)");
        break;
    }
    case UYG_DEDE: {
        int sv = ses_var();
        ek(bek, sizeof bek, !sv ? "Ses kartı: YOK — seslendirilemez" : ses_durum()->hda ? "Ses kartı: var (HDA)" : "Ses kartı: var (AC'97)");
        BEKLE((sinama_satir_ocr(id, 14, 40, 40, sv ? R_VURGU : R_UYARI, R_ARKA, oku, on), esit(oku, bek)), 3 * hz);
        ok = esit(oku, bek);
        /* ilk soze tikla: ses varsa "Söylendi.", yoksa durumu soyler */
        const ulgen_yuzey_t *ys = ulgen_yuzey_al(id);
        if (ok && ys) {
            uint64_t once = ses_durum()->calinan_ornek;
            t_tikla(ys->x + 60, ys->y + 70 + 6);
            const char *d = sv ? "Söylendi." : "Ses kartı yok: söylenemedi.";
            char s[64];
            BEKLE((sinama_satir_ocr(id, 14, 440 - 80 + 40, 30, sv ? R_VURGU : R_UYARI, R_ARKA, s, sizeof s), esit(s, d)), 40 * hz);
            if (!esit(s, d)) { ok = 0; serial_write("UYG-TEST:   dede durum='"); serial_write(s); serial_write("'\n"); }
            /* ses varsa: sentez kareleri karta verildi VE kart hepsini caldi */
            (void)once;
            uint64_t kare = dede_son_kare();   /* son konusma: tiklanan soz */
            int birim = dede_birim_sayisi("Damlaya damlaya göl olur.") - 1;   /* '.' bosluk degil ama birim de degil */
            BEKLE(ses_bosaldi(), 5 * hz);
            int bosaldi = ses_bosaldi();
            serial_write("UYG-TEST:   dede kare="); yaz_sayi(kare);
            serial_write(" (~"); yaz_sayi(kare * 1000 / SES_ORNEK_HIZ); serial_write(" ms) bosaldi="); yaz_sayi((uint64_t)bosaldi); serial_write("\n");
            serial_write("UYG-TEST:   dede birim="); yaz_sayi(dede_son_birim()); serial_write(" beklenen="); yaz_sayi((uint64_t)birim); serial_write("\n");
            /* her birim 45..180 ms: soz uzunlugundan cikan alt/ust sinir */
            if (sv && (kare < (uint64_t)birim * SES_ORNEK_HIZ * 45 / 1000 ||
                       kare > (uint64_t)(birim + 4) * SES_ORNEK_HIZ * 180 / 1000 ||
                       dede_son_birim() != (uint32_t)birim || !bosaldi)) ok = 0;
            if (!sv && kare != 0) ok = 0;
            /* 1.4.9: bilinen dalga (1000 Hz kare, +-8000, 300 ms = 14400 kare);
             * ana makine WAV kaydinda ORNEK ORNEK arar (AC'97 ve HDA) */
            if (sv) {
                int r = ses_ton(1000, 300);
                BEKLE(ses_bosaldi(), 5 * hz);
                serial_write("SES-TON-TEST: 1000 Hz 300 ms kare=14400 sonuc="); yaz_sayi((uint64_t)(r == 0));
                serial_write(" bosaldi="); yaz_sayi((uint64_t)ses_bosaldi()); serial_write("\n");
                if (r != 0 || !ses_bosaldi()) ok = 0;
                ses_bitir();
            }
        }
        break;
    }
    case UYG_TIMUR: {
        int nx = (int)((rdmsr_(0xC0000080) >> 11) & 1);
        int smep = (int)((cr4_() >> 20) & 1);
        ek(bek, sizeof bek, nx ? "açık" : "KAPALI");
        BEKLE((sinama_satir_ocr(id, 250, 44, 10, nx ? R_VURGU : 0xD9534Fu, R_ARKA, oku, on), esit(oku, bek)), 3 * hz);
        ok = esit(oku, bek);
        char s[24];
        sinama_satir_ocr(id, 250, 164, 10, smep ? R_VURGU : R_UYARI, R_ARKA, s, sizeof s);
        ok = ok && esit(s, smep ? "açık" : "kapalı");       /* SMEP durumu dürüst */
        uint64_t cr0; __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        int wx = nx && ((cr0 >> 16) & 1);
        char w[32];
        sinama_satir_ocr(id, 250, 84, 22, wx ? R_VURGU : 0xD9534Fu, R_ARKA, w, sizeof w);
        ok = ok && esit(w, wx ? "uygulanıyor" : ((cr0 >> 16) & 1) ? "EKSİK (NX yok)" : "EKSİK (CR0.WP kapalı)");
        break;
    }
    case UYG_KUT: {
        kut_user_t u;
        if (kut_kullanici_al(0, &u) != 0) { ek(bek, sizeof bek, "(tanımlı kullanıcı yok)"); }
        else { ek(bek, sizeof bek, u.name); }
        BEKLE((sinama_satir_ocr(id, 64, 84, 15, R_YAZI, R_ARKA, oku, on), esit(oku, bek)), 3 * hz);
        ok = esit(oku, bek);
        char s[32];
        sinama_satir_ocr(id, 192, 84, 15, u.rol == 0 ? R_UYARI : R_YAZI, R_ARKA, s, sizeof s);
        ok = ok && esit(s, kut_rol_name(u.rol));
        break;
    }
    case UYG_ASENA: {
        asena_rapor_t r;
        asena_tara(0, &r);
        if (r.bulgu == 0) ek(bek, sizeof bek, "Dosya sistemi sağlam: sorun yok.");
        else { ek_sayi(bek, sizeof bek, r.bulgu); ek(bek, sizeof bek, " sorun bulundu:"); }
        BEKLE((sinama_satir_ocr(id, 16, 96, 40, r.bulgu ? R_UYARI : R_VURGU, R_ARKA, oku, on), esit(oku, bek)), 5 * hz);
        ok = esit(oku, bek);
        break;
    }
    case UYG_ERGENEKON: {
        ergenekon_durum_t d;
        ergenekon_durum(&d);
        int y = 66; uint32_t renk = R_VURGU;
        if (!d.var) { ek(bek, sizeof bek, "Dizge imajı yok (ISO'daki /boot/ergenekon.krv yüklenmedi)."); y = 44; renk = 0xD9534Fu; }
        else if (d.gecerli) { ek(bek, sizeof bek, "İmza: "); ek(bek, sizeof bek, d.anahtar_no == 0 ? "üretim anahtarı" : "sınama anahtarı"); ek(bek, sizeof bek, " · doğrulandı"); }
        else { ek(bek, sizeof bek, "İmza: GEÇERSİZ — "); ek(bek, sizeof bek, d.neden); renk = 0xD9534Fu; }
        BEKLE((sinama_satir_ocr(id, 16, y, 70, renk, R_ARKA, oku, on), esit(oku, bek)), 5 * hz);
        ok = esit(oku, bek);
        break;
    }
    case UYG_TAMGA: {
        extern const char cekirdek_metin_bas[], cekirdek_metin_son[], cekirdek_salt_bas[], cekirdek_salt_son[];
        br_sha256_context c; uint8_t o[32];
        br_sha256_init(&c);
        br_sha256_update(&c, cekirdek_metin_bas, (size_t)(cekirdek_metin_son - cekirdek_metin_bas));
        br_sha256_update(&c, cekirdek_salt_bas, (size_t)(cekirdek_salt_son - cekirdek_salt_bas));
        br_sha256_out(&c, o);
        for (int i = 0; i < 32; i++) { bek[2 * i] = "0123456789abcdef"[o[i] >> 4]; bek[2 * i + 1] = "0123456789abcdef"[o[i] & 15]; }
        bek[64] = 0;
        serial_write("TAMGA-TEST: cekirdek ozeti="); serial_write(bek); serial_write("\n");
        BEKLE((sinama_satir_ocr(id, 30, 60, 64, R_YAZI, R_ARKA, oku, on), esit(oku, bek)), 5 * hz);
        ok = esit(oku, bek);
        break;
    }
    case UYG_TORE: {
        /* Gorevler gorunumu basligi, sonra "Cagrilar" sekmesi (64 ayri) */
        ek(bek, sizeof bek, "kimlik  ad              Ring3  yetkiler");
        BEKLE((sinama_satir_ocr(id, 14, 70, 45, R_SONUK, R_ARKA, oku, on), esit(oku, bek)), 3 * hz);
        ok = esit(oku, bek);
        break;
    }
    }
    if (!ok) { serial_write("UYG-TEST:   beklenen='"); serial_write(bek); serial_write("'\n"); }
    return ok;
}

static int tore_cagrilar(int id, uint32_t hz) {
    const ulgen_yuzey_t *ys = ulgen_yuzey_al(id);
    if (!ys) return 0;
    t_tikla(ys->x + 150, ys->y + 44);                       /* "Çağrılar" sekmesi */
    char s[64];
    BEKLE((sinama_satir_ocr(id, 14, 70, 20, R_SONUK, R_ARKA, s, sizeof s), s[0] == 'n' && s[1] == 'o'), 3 * hz);
    if (!(s[0] == 'n' && s[1] == 'o')) return 0;
    /* gorunen ilk 20 satirdan yetki isteyen ilk cagri: ad ve yetki */
    for (int n = 0; n < 20; n++) {
        int64_t y = syscall_gereken_yetki((uint64_t)n);
        const char *ad = syscall_politika_adi((uint64_t)n);
        if (y <= 0 || !ad) continue;
        int sy = 70 + 18 + n * 18;
        char oad[48], oy[128], bek[128];
        sinama_satir_ocr(id, 46, sy, 30, R_YAZI, R_ARKA, oad, sizeof oad);
        sinama_satir_ocr(id, 310, sy, 55, R_VURGU, R_ARKA, oy, sizeof oy);
        yetki_dizisi((uint32_t)y, bek, sizeof bek);
        serial_write("UYG-TEST:   tore cagri "); yaz_sayi((uint64_t)n); serial_write(" '"); serial_write(oad);
        serial_write("' '"); serial_write(oy); serial_write("'\n");
        return esit(oad, ad) && esit(oy, bek);
    }
    return 0;
}

static volatile int su_bitti;
int sinama_uyg_bitti(void) { return __atomic_load_n(&su_bitti, __ATOMIC_ACQUIRE); }

static void gorev(void) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    BEKLE(sinama148_bitti(), 120 * hz);
    BEKLE(sinama149_bitti(), 240 * hz);
    BEKLE(sinama_kervan_bitti(), 120 * hz);
    BEKLE(sinama_asena_bitti(), 300 * hz);
    if (sinama_asena_mod() == 1) {      /* ASENA diski: dizge dosyalari kasten bozuk */
        serial_write("UYG-TEST: atlandi (ASENA MOD 1 diski)\n");
        __atomic_store_n(&su_bitti, 1, __ATOMIC_RELEASE);
        thread_exit();
    }
    thread_sleep(hz / 2);
    static const hedef_t H[] = {
        { 10, UYG_ULAK,  TORE_PROFILE_ULAK_UYG,  "ULAK",  "ULAK — Ağ Durumu" },
        {  2, UYG_DEDE,  TORE_PROFILE_DEDE_UYG,  "DEDE",  "DEDE KORKUT — Söz" },
        { 12, UYG_TORE,  TORE_PROFILE_TORE_UYG,  "TORE",  "TÖRE — Yetkiler ve Politikalar" },
        {  5, UYG_TIMUR, TORE_PROFILE_TIMUR_UYG, "TIMUR", "TİMUR — Korumalar" },
        {  6, UYG_KUT,   TORE_PROFILE_KUT_UYG,   "KUT",   "KUT — Kimlik ve Yetki" },
        {  7, UYG_ASENA, TORE_PROFILE_ASENA_UYG, "ASENA", "ASENA — Kurtarma" },
        {  8, UYG_ERGENEKON, TORE_PROFILE_ERGENEKON_UYG, "ERGENEKON", "ERGENEKON — Yeniden Kurulum" },
        { 13, UYG_TAMGA, TORE_PROFILE_TAMGA_UYG, "TAMGA", "TAMGA — Dijital Kimlik" },
    };
    int toplam = 0;
    for (unsigned i = 0; i < sizeof H / sizeof H[0]; i++) {
        const hedef_t *t = &H[i];
        int h = 0;
        uint32_t once = uygulama_baslatma_sayisi(t->uyg);
        if (menuden_ac(t->tamga) != 0) h |= 1;
        BEKLE(uygulama_penceresi(t->uyg) >= 0, 10 * hz);
        int id = uygulama_penceresi(t->uyg);
        if (id < 0 || uygulama_baslatma_sayisi(t->uyg) != once + 1) h |= 1;
        char oku[160]; oku[0] = 0;
        if (id >= 0) {
            uint32_t g = uygulama_gorev(t->uyg);
            if (!g || tore_get_capabilities(g) != t->profil) h |= 2;
            BEKLE((sinama_satir_ocr(id, 14, 7, 40, R_VURGU, R_PANO, oku, sizeof oku), esit(oku, t->baslik)), 5 * hz);
            if (!esit(oku, t->baslik)) { h |= 4; serial_write("UYG-TEST:   baslik='"); serial_write(oku); serial_write("'\n"); }
            char an[160]; an[0] = 0;
            if (!anahtar(t, id, hz, an, sizeof an)) h |= 8;
            if (t->uyg == UYG_TORE && !tore_cagrilar(id, hz)) h |= 64;
            serial_write("UYG-TEST: "); serial_write(t->ad); serial_write(" acik (goruntu)\n");
            thread_sleep(hz / 2);
            serial_write("UYG-TEST: "); serial_write(t->ad); serial_write(" baslik='"); serial_write(oku);
            serial_write("' anahtar='"); serial_write(an); serial_write("' yetki=");
            yaz_sayi(g ? tore_get_capabilities(g) : 0);
            serial_write(" profil="); yaz_sayi(t->profil); serial_write("\n");
            kapat(id, t->uyg, hz, &h);
        }
        serial_write("UYG-TEST: "); serial_write(t->ad); serial_write(" hata="); yaz_sayi((uint64_t)h); serial_write("\n");
        toplam |= h;
        thread_sleep(hz / 5);
    }
    /* 1.4.9: artik sonuk tamga yok (15'in hepsi calisir). Menunun satir
     * disindaki ic payina tiklama hicbir sey baslatmamali ve menuyu kapatmali. */
    uint32_t sayac[UYG_SAYI];
    for (int u = 0; u < UYG_SAYI; u++) sayac[u] = uygulama_baslatma_sayisi(u);
    int ux, uy, uw, uh;
    ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
    t_tikla(ux + 30, ULGEN_UST_BAR / 2);
    thread_sleep(2);
    int r = ulgen_theme_uyg_menu_acik() ? 0 : -1;
    t_tikla(ux + 40, uy + 2);                        /* ust ic pay: satir degil */
    thread_sleep(hz / 2);
    int degisti = 0;
    for (int u = 0; u < UYG_SAYI; u++) if (uygulama_baslatma_sayisi(u) != sayac[u]) degisti = 1;
    if (r != 0 || degisti || ulgen_theme_uyg_menu_acik()) toplam |= 32;
    /* menu disina tiklama menuyu kapatir */
    t_tikla(ux + 30, ULGEN_UST_BAR / 2);
    int acildi = ulgen_theme_uyg_menu_acik();
    /* ekran goruntusu icin menu 2 sn acik kalir (tests: QEMU screendump) */
    serial_write("UYG-TEST: menu acik (goruntu)\n");
    thread_sleep(2 * hz);
    t_tikla(ux + uw + 200, uy + uh + 40);
    if (!acildi || ulgen_theme_uyg_menu_acik()) toplam |= 32;
    serial_write("UYG-TEST: Uygulamalar menusu -> ULAK, DEDE, TORE, TIMUR, KUT, ASENA, ERGENEKON, TAMGA (OCR, yetki, kapat) + menu ic payi hata=");
    yaz_sayi((uint64_t)toplam);
    serial_write(toplam ? " -> HATA\n" : " -> BASARILI\n");
    __atomic_store_n(&su_bitti, 1, __ATOMIC_RELEASE);
    thread_exit();
}

void sinama_uyg_baslat(void) { thread_create("UygSinama", gorev); }
