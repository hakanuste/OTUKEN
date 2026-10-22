/* =====================================================================
 * 1.4.8 — UYGULAMA-TEST: masaüstü uygulamaları uçtan uca (yalnız TANI)
 *
 * Sahte yol YOK: tıklamalar kullanıcının faresiyle AYNI işleyiciden
 * (gui_fare_isle) geçer; uygulama diskteki ELF'ten KENDİ adres alanında
 * Ring3'te koşar; tema değişikliği uygulamanın kendi SYS_TEMA çağrısıyla
 * olur; kapanış pencerenin kapatma düğmesine tıklamaktır.
 *
 * Denetimler (her biri ayrı bit):
 *   Görünüm (rıhtımdaki ÜLGEN tamgası):
 *     1 süreç başladı ve penceresi açıldı
 *     2 KENDİ adres alanı (çekirdeğinkinden ayrı), görev Ring3 kullanıcısı
 *     4 TÖRE profili tam olarak GORUNUM_UYG
 *     8 uygulama kendi karesini çizip sundu (bilinen pikseller)
 *    16 ikinci tıklama yeni süreç açmadı (tek kopya; başlatma sayacı 1 kaldı)
 *    32 temaya tıklama temayı değiştirdi; geri tıklama eski temaya döndü
 *    64 kapat düğmesi: süreç bitti, pencere silindi, adres alanı geri alındı
 *       (uygulamanın KENDİ PML4'ü, aynı kimlikle artık canlı değil)
 *   Sistem (üst çubuk "Sistem" ve ÖTÜKEN tamgası):
 *   128 süreç + pencere, profil tam olarak SISTEM_UYG (AYAR YOK)
 *   256 kendi karesini sundu
 *   512 ÖTÜKEN tamgası ikinci kopya açmadı
 *  1024 kapat düğmesi: süreç bitti, pencere silindi, adres alanı geri alındı
 * ===================================================================== */
#include "sinama148.h"
#include "uygulama.h"
#include "gui.h"
#include "ulgen.h"
#include "ulgen_theme.h"
#include "tema.h"
#include "tore.h"
#include "process.h"
#include "sched.h"
#include "thread.h"
#include "vmm.h"
#include "timer.h"
#include "serial.h"
#include "gorsel_kilit.h"
#include "mutex.h"

#define BEKLE(kosul, tik) do { uint64_t _t0 = timer_get_ticks(); \
    while (!(kosul) && timer_get_ticks() - _t0 < (tik)) thread_sleep(1); } while (0)

/* Uygulamaların kendi renkleri (userprog/sistem.c, gorunum.c) */
#define UYG_ARKA 0x14232Bu
#define UYG_PANO 0x1B313Au
/* Görünüm düğme yerleşimi (userprog/gorunum.c) */
#define G_DUG_Y0  48
#define G_DUG_ARA 44

typedef struct { char b[512]; int n; } sat_t;
static void s_ek(sat_t *s, const char *t) { while (*t && s->n < 510) s->b[s->n++] = *t++; s->b[s->n] = 0; }
static void s_sayi(sat_t *s, uint64_t v) {
    char t[24]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) { char c[2] = { t[--n], 0 }; s_ek(s, c); }
}

static void tikla(int x, int y) {
    gui_fare_isle(x, y, 1);
    thread_sleep(1);
    gui_fare_isle(x, y, 0);
}

/* Yüzeyin sunulan karesinde (içerik koordinatı) piksel; yoksa 0xFFFFFFFF */
static uint32_t sunulan_piksel(int id, int x, int y) {
    uint32_t p = 0xFFFFFFFFu;
    ozkilit_al(&gorsel_kilit);
    const ulgen_yuzey_t *s = ulgen_yuzey_al(id);
    if (s && s->sunulan && x >= 0 && y >= 0 && x < s->w && y < s->h)
        p = s->sunulan[y * s->w + x] & 0xFFFFFFu;
    ozkilit_birak(&gorsel_kilit);
    return p;
}

static int yuzey_konum(int id, int *x, int *y, int *w) {
    ozkilit_al(&gorsel_kilit);
    const ulgen_yuzey_t *s = ulgen_yuzey_al(id);
    int ok = s && s->used;
    if (ok) { *x = s->x; *y = s->y; *w = s->w; }
    ozkilit_birak(&gorsel_kilit);
    return ok ? 0 : -1;
}

/* Kapat düğmesinin ekran noktası (ulgen_kapat_dugmesi_mi ile doğrulanır) */
static int kapat_noktasi(int id, int *kx, int *ky) {
    int x, y, w;
    if (yuzey_konum(id, &x, &y, &w) != 0) return -1;
    *kx = x + w - 10;
    *ky = y - ULGEN_BASLIK_YUK / 2;
    return ulgen_kapat_dugmesi_mi(id, *kx, *ky) ? 0 : -1;
}

static int umay_penceresi(void) {
    int r = -1;
    ozkilit_al(&gorsel_kilit);
    for (int i = 0; i < ULGEN_MAX_YUZEY && r < 0; i++) {
        const ulgen_yuzey_t *s = ulgen_yuzey_al(i);
        if (s && s->used && s->baslik[0] == 'U' && s->baslik[1] == 'M' &&
            s->baslik[2] == 'A' && s->baslik[3] == 'Y') r = i;
    }
    ozkilit_birak(&gorsel_kilit);
    return r;
}

/* Uygulamanın adres alanı ayrı mı, görev Ring3 kullanıcısı mı */
static int ayri_alan(int u) {
    uint32_t pid = uygulama_pid(u);
    process_t *p = pid ? process_find(pid) : 0;
    if (!p || !p->task) return 0;
    return p->address_space != 0 && p->task->cr3 == p->address_space &&
           p->task->adres_sahibi == 1 && p->task->kullanici == 1;
}

/* Uygulamanın PML4'ü ve o alanın canlı kimliği (sıra no) */
static uint64_t alan_al(int u, uint64_t *kimlik) {
    uint32_t pid = uygulama_pid(u);
    process_t *p = pid ? process_find(pid) : 0;
    uint64_t pml4 = p ? p->address_space : 0;
    *kimlik = pml4 ? vmm_alan_kimlik(pml4) : 0;
    return pml4;
}

static int menu_x(int sira) {
    for (int x = 0; x < 1024; x++)
        if (ulgen_theme_menu_hit(x, 20) == sira) return x + 8;
    return -1;
}

static volatile int sonuc = -1;
int sinama148_bitti(void) { return sonuc != -1; }
static sat_t rapor;

static void sinama_gorevi(void) {
    sat_t *s = &rapor;
    s->n = 0; s->b[0] = 0;
    int hata = 0;
    s_ek(s, "UYGULAMA-TEST: rihtim/ust cubuk -> diskten ELF -> ayri surec Ring3");

    /* UMAY penceresi sonradan açılıp tıklama noktalarını örtmesin */
    BEKLE(umay_penceresi() >= 0, 3000);
    thread_sleep(20);

    /* ---------------- Görünüm: rıhtımdaki ÜLGEN tamgası ---------------- */
    int rx = -1, ry = -1;
    for (int k = 0; k < 7 && rx < 0; k++) {
        int x, y, b;
        if (ulgen_theme_rihtim_konum(k, &x, &y, &b) < 0) continue;
        if (ulgen_theme_dock_hit(x + b / 2, y + b / 2) == 3) { rx = x + b / 2; ry = y + b / 2; }
    }
    if (rx < 0 || ulgen_noktadaki_yuzey(rx, ry) >= 0) {
        s_ek(s, " [ULGEN tamgasi bulunamadi ya da ortulu]");
        hata |= 1;
    } else {
        tikla(rx, ry);
    }
    BEKLE(uygulama_penceresi(UYG_GORUNUM) >= 0, 500);
    int gp = uygulama_penceresi(UYG_GORUNUM);
    uint32_t gg = uygulama_gorev(UYG_GORUNUM);
    if (gp < 0) hata |= 1;
    if (!ayri_alan(UYG_GORUNUM)) hata |= 2;
    if (!gg || tore_get_capabilities(gg) != TORE_PROFILE_GORUNUM_UYG) hata |= 4;
    if (gp >= 0) {
        BEKLE(sunulan_piksel(gp, 4, 4) == UYG_PANO && sunulan_piksel(gp, 4, 40) == UYG_ARKA, 200);
        if (sunulan_piksel(gp, 4, 4) != UYG_PANO || sunulan_piksel(gp, 4, 40) != UYG_ARKA) hata |= 8;
    }
    s_ek(s, " gorunum(pid="); s_sayi(s, uygulama_pid(UYG_GORUNUM));
    s_ek(s, " pencere="); s_sayi(s, (uint64_t)(gp + 1));
    s_ek(s, " yetki=0x");
    {
        uint32_t y = gg ? tore_get_capabilities(gg) : 0;
        for (int i = 3; i >= 0; i--) { char c[2] = { "0123456789ABCDEF"[(y >> (i * 4)) & 15], 0 }; s_ek(s, c); }
    }
    s_ek(s, ")");

    /* tek kopya */
    /* (Genel süreç sayısıyla ölçülmez: TaskD gibi ilgisiz süreçler bu
     * arada ölebilir - ilk tam koşuda böyle yanlış HATA verdi.) */
    uint32_t pid1 = uygulama_pid(UYG_GORUNUM);
    uint32_t bas1 = uygulama_baslatma_sayisi(UYG_GORUNUM);
    int ikinci = 0;
    if (rx >= 0 && ulgen_noktadaki_yuzey(rx, ry) < 0) { tikla(rx, ry); ikinci = 1; }
    thread_sleep(30);
    if (!ikinci || bas1 != 1 || uygulama_baslatma_sayisi(UYG_GORUNUM) != bas1 ||
        uygulama_pid(UYG_GORUNUM) != pid1 || uygulama_penceresi(UYG_GORUNUM) != gp) hata |= 16;

    /* tema: başka bir temaya tıkla, sonra eskisine */
    const tema_t *t0 = tema_simdiki();
    int hedef = -1, eski = -1;
    for (int i = 0; i < tema_sayisi(); i++) {
        if (tema_sirali(i) == t0) eski = i;
        else if (hedef < 0) hedef = i;
    }
    int tema_ok = 0;
    int gx, gy, gw;
    if (gp >= 0 && hedef >= 0 && eski >= 0 && yuzey_konum(gp, &gx, &gy, &gw) == 0) {
        int bx = gx + 40, by = gy + G_DUG_Y0 + hedef * G_DUG_ARA + 10;
        if (ulgen_noktadaki_yuzey(bx, by) == gp) {
            tikla(bx, by);
            BEKLE(tema_simdiki() == tema_sirali(hedef), 300);
            int degisti = tema_simdiki() == tema_sirali(hedef);
            int bx2 = gx + 40, by2 = gy + G_DUG_Y0 + eski * G_DUG_ARA + 10;
            tikla(bx2, by2);
            BEKLE(tema_simdiki() == t0, 300);
            tema_ok = degisti && tema_simdiki() == t0;
            s_ek(s, " tema:"); s_ek(s, tema_sirali(hedef)->ad);
            s_ek(s, degisti ? "(oldu)" : "(OLMADI)");
            s_ek(s, "->"); s_ek(s, t0->ad);
            s_ek(s, tema_simdiki() == t0 ? "(dondu)" : "(DONMEDI)");
        } else s_ek(s, " [tema dugmesi ortulu]");
    }
    if (!tema_ok) hata |= 32;

    /* kapat: genel sayaçlarla ölçülmez (TaskD gibi ilgisiz süreçler
     * sınama sırasında ölüp kendi alanlarını bırakabilir; ilk tam koşuda
     * böyle yanlış HATA verdi). Ölçülen, BU uygulamanın adres alanıdır:
     * kapanıştan sonra aynı kimlikle canlı olmamalı. */
    int kx, ky;
    uint64_t gkim = 0;
    uint64_t gpml4 = alan_al(UYG_GORUNUM, &gkim);
    if (gp >= 0 && kapat_noktasi(gp, &kx, &ky) == 0 && ulgen_noktadaki_yuzey(kx, ky) == gp) {
        tikla(kx, ky);
        BEKLE(uygulama_pid(UYG_GORUNUM) == 0, 500);
    }
    int yuzey_gitti;
    { uint32_t sh; yuzey_gitti = (gp >= 0) && ulgen_yuzey_sahibi(gp, &sh) != 0; }
    int alan_gitti = gpml4 && gkim && vmm_alan_kimlik(gpml4) != gkim;
    if (uygulama_pid(UYG_GORUNUM) != 0 || !yuzey_gitti || !alan_gitti) hata |= 64;
    s_ek(s, " kapat(surec_bitti="); s_sayi(s, uygulama_pid(UYG_GORUNUM) == 0);
    s_ek(s, " pencere_silindi="); s_sayi(s, (uint64_t)yuzey_gitti);
    s_ek(s, " alan_geri_alindi="); s_sayi(s, (uint64_t)alan_gitti);
    s_ek(s, ")");

    /* ---------------- Sistem: üst çubuk "Sistem" ---------------- */
    int mx = menu_x(ULGEN_MENU_SISTEM);
    if (mx >= 0) tikla(mx, 20);
    BEKLE(uygulama_penceresi(UYG_SISTEM) >= 0, 500);
    int sp = uygulama_penceresi(UYG_SISTEM);
    uint32_t sg = uygulama_gorev(UYG_SISTEM);
    if (sp < 0 || !ayri_alan(UYG_SISTEM) || !sg ||
        tore_get_capabilities(sg) != TORE_PROFILE_SISTEM_UYG ||
        (tore_get_capabilities(sg) & KUT_AYAR)) hata |= 128;
    if (sp >= 0) {
        BEKLE(sunulan_piksel(sp, 4, 4) == UYG_PANO && sunulan_piksel(sp, 4, 40) == UYG_ARKA, 200);
        if (sunulan_piksel(sp, 4, 4) != UYG_PANO || sunulan_piksel(sp, 4, 40) != UYG_ARKA) hata |= 256;
    }
    uint32_t spid = uygulama_pid(UYG_SISTEM);
    uint32_t sbas = uygulama_baslatma_sayisi(UYG_SISTEM);
    tikla(30, 20);                                   /* ÖTÜKEN tamgası */
    thread_sleep(30);
    if (ulgen_theme_menu_hit(30, 20) != ULGEN_MENU_TAMGA || sbas != 1 ||
        uygulama_pid(UYG_SISTEM) != spid || uygulama_baslatma_sayisi(UYG_SISTEM) != sbas) hata |= 512;
    s_ek(s, " sistem(pid="); s_sayi(s, spid);
    s_ek(s, " pencere="); s_sayi(s, (uint64_t)(sp + 1)); s_ek(s, ")");
    uint64_t skim = 0;
    uint64_t spml4 = alan_al(UYG_SISTEM, &skim);
    if (sp >= 0 && kapat_noktasi(sp, &kx, &ky) == 0 && ulgen_noktadaki_yuzey(kx, ky) == sp) {
        tikla(kx, ky);
        BEKLE(uygulama_pid(UYG_SISTEM) == 0, 500);
    }
    int salan_gitti = spml4 && skim && vmm_alan_kimlik(spml4) != skim;
    { uint32_t sh; if (uygulama_pid(UYG_SISTEM) != 0 || sp < 0 || ulgen_yuzey_sahibi(sp, &sh) == 0 ||
                       !salan_gitti) hata |= 1024; }
    s_ek(s, " sistem_kapat(alan_geri_alindi="); s_sayi(s, (uint64_t)salan_gitti); s_ek(s, ")");

    s_ek(s, " hata="); s_sayi(s, (uint64_t)hata);
    s_ek(s, hata ? " -> HATA\n" : " -> BASARILI\n");
    serial_write(s->b);
    sonuc = hata;
    thread_exit();
}

void sinama148_uygulama_baslat(void) {
    thread_create("UygSinama", sinama_gorevi);
}
