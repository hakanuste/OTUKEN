/* =====================================================================
 * 1.4.9 — AG-TEST (yalniz TANI)
 * Ring3 AGSINA.ELF (userprog/agsina.c) KENDI adres alaninda, yalniz ag +
 * disk okuma + ekran yetkisiyle kosar ve yerel sunucuya (tests/ag) karsi
 * HTTP/TCP'yi uctan uca dener; kendi sonucunu "AGSINA SONUC:" satiriyla
 * yazar. Bu gorev, program bittikten sonra CEKIRDEK tarafini denetler:
 *   1 program basladi, GERCEKTEN ag kullandi (>= 10 Ring3 baglantisi
 *     kuruldu) ve bitti (zaman asimi yok)
 *   2 olen programin ACIK biraktigi baglanti reap ile birakildi:
 *     Ring3 tutamagi kalmadi
 *   4 TCP yuvalari sizmadi (kapanis zaman asimi icinde geri dondu)
 *   8 saglama toplami hatali segment gelmedi (bozuk paket yok)
 *  16 esanli iki DNS sorusu karismadi: '.invalid' sorusu, ayni anda sorulan
 *     baska adin adresini ALMADI (1.4.9'da bulunan yaris; bkz. dns.c)
 * ===================================================================== */
#include "sinama149.h"
#include "kervan2.h"
#include "fat16.h"
#include "tema.h"
#include "tore.h"

#include "sinama148.h"
#include "uygulama.h"
#include "ag.h"
#include "tcp.h"
#include "dhcp.h"
#include "dns.h"
#include "vfs.h"
#include "thread.h"
#include "timer.h"
#include "serial.h"
#include "ulak.h"
#include "gui.h"
#include "ulgen.h"
#include "ulgen_theme.h"
#include "keyboard.h"
#include "gorsel_kilit.h"
#include "mutex.h"
#include "yazi16.h"
#include "fb.h"

#define BEKLE(kosul, tik) do { uint64_t _t0 = timer_get_ticks(); \
    while (!(kosul) && timer_get_ticks() - _t0 < (tik)) thread_sleep(1); } while (0)

static void yaz_sayi(uint64_t v) {
    char t[24]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char c[24]; int k = 0;
    while (n) c[k++] = t[--n];
    c[k] = 0;
    serial_write(c);
}

static void tulpar_sinamasi(const char *sunucu, const char *port, const char *tls_port);
static void kervan_ag_sinamasi(const char *sunucu, const char *port);
static void yaz_sayi(uint64_t v);

static volatile int s149_bitti = 0;
int sinama149_bitti(void) { return __atomic_load_n(&s149_bitti, __ATOMIC_ACQUIRE); }
static void bitir(void) { __atomic_store_n(&s149_bitti, 1, __ATOMIC_RELEASE); thread_exit(); }

static void gorev(void) {
    int fd = vfs_open("/AGSINA.ELF");
    if (fd < 0) {
        serial_write("AG-TEST: atlandi (sinama diskinde AGSINA.ELF yok; bkz. tests/ag)\n");
        bitir();
    }
    vfs_close(fd);
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    BEKLE(dhcp_get_config()->configured, 10 * hz);
    /* esanli DNS: iki soru ayni anda havada */
    int dns_hata = 0;
    {
        const uint8_t *sv = dhcp_get_config()->dns;
        int ha = dns_sor("example.com", sv);
        int hb = dns_sor("yok-boyle-bir-ad.invalid", sv);
        uint8_t ia[4], ib[4];
        int da = 0, db = 0;
        uint64_t t0 = timer_get_ticks();
        while ((da == 0 || db == 0) && timer_get_ticks() - t0 < 4 * hz) {
            ag_yokla();
            thread_sleep(1);
            if (!da) da = dns_sonuc(ha, ia);
            if (!db) db = dns_sonuc(hb, ib);
        }
        if (ha < 0 || hb < 0 || db == 1) dns_hata = 1;
        serial_write("AG-TEST: esanli DNS example.com=");
        serial_write(da == 1 ? "cozuldu" : da == -1 ? "yok" : "yanitsiz");
        serial_write(" .invalid=");
        serial_write(db == 1 ? "COZULDU(YANLIS)" : db == -1 ? "yok" : "yanitsiz");
        serial_write("\n");
        dns_birak(ha); dns_birak(hb);
    }
    int yuva0 = tcp_acik_sayisi();
    uint32_t red0 = tcp_saglama_reddi();
    uint32_t bag0 = ag_baglanti_toplam();
    int hata = dns_hata ? 16 : 0;
    if (uygulama_ac(UYG_AGSINA) != 1) hata |= 1;
    BEKLE(uygulama_baslatma_sayisi(UYG_AGSINA) == 1 && uygulama_pid(UYG_AGSINA) == 0, 150 * hz);
    uint32_t bag = ag_baglanti_toplam() - bag0;
    if (uygulama_pid(UYG_AGSINA) != 0 || bag < 10) hata |= 1;
    BEKLE(ag_tutamak_sayisi() == 0, 5 * hz);
    int tut = ag_tutamak_sayisi();
    if (tut != 0) hata |= 2;
    BEKLE(tcp_acik_sayisi() <= yuva0, 15 * hz);
    int yuva1 = tcp_acik_sayisi();
    if (yuva1 > yuva0) hata |= 4;
    uint32_t red = tcp_saglama_reddi() - red0;
    if (red != 0) hata |= 8;
    serial_write("AG-TEST: Ring3 TCP/HTTP uctan uca, olumde birakma, yuva sizintisi (tutamak=");
    yaz_sayi((uint64_t)tut);
    serial_write(" tcp_yuva ");
    yaz_sayi((uint64_t)yuva0); serial_write("->"); yaz_sayi((uint64_t)yuva1);
    serial_write(" saglama_reddi="); yaz_sayi(red);
    serial_write(" baglanti="); yaz_sayi(bag);
#ifdef OTUKEN_AG_KAYIP
    serial_write(" dusurulen_tcp="); yaz_sayi(ulak_kayip_sayisi());
    if (ulak_kayip_sayisi() == 0) hata |= 32;      /* kayip hic uygulanmadiysa sinama gecersiz */
#endif
    serial_write(") hata="); yaz_sayi((uint64_t)hata);
    serial_write(hata ? " -> HATA\n" : " -> BASARILI\n");
    /* sunucu ve port: /AGSINA.CFG'nin ilk iki alani */
    {
        static char cfg[320];
        int f = vfs_open("/AGSINA.CFG");
        int k = f >= 0 ? vfs_read(f, cfg, sizeof cfg - 1) : -1;
        if (f >= 0) vfs_close(f);
        if (k > 0) {
            cfg[k] = 0;
            char *sv = cfg, *pt = cfg, *tp = 0;
            for (char *q = cfg; *q; q++)
                if (q[0] == '\n' && q[1] == 'T' && q[2] == 'L' && q[3] == 'S' && q[4] == ' ') {
                    tp = q + 5; char *e = tp; while (*e >= '0' && *e <= '9') e++; *e = 0; break;
                }
            while (*pt && *pt != ' ') pt++;
            if (*pt) { *pt++ = 0; char *e = pt; while (*e && *e != ' ') e++; *e = 0; tulpar_sinamasi(sv, pt, tp); kervan_ag_sinamasi(sv, pt); }
        } else serial_write("TULPAR-TEST: AGSINA.CFG okunamadi -> HATA\n");
    }
    bitir();
}

/* ===================== TULPAR-TEST (gezgin uctan uca) =====================
 * Kullanici gibi: rihtimdaki TULPAR'a tikla, adres cubuguna tikla, adresi
 * KLAVYE kuyrugundan yaz, Enter; sayfayi pencerenin SUNULAN karesinden
 * glif glif OKU (OCR). Sonra klavyeyle bagalanti sec+ac, Backspace ile geri,
 * pencereyi kapat; guvenilmeyen HTTPS acilmaz (hata sayfasi + neden).
 * Renkler ve yerlesim userprog/tulpar.c ile ayni. */
#define T_UST 34
#define T_SOL 10
#define T_SATIR 18
#define T_KAGIT   0xF3EEE2u
#define T_YAZI    0x1E262Bu
#define T_BASLIK  0x8A3413u
#define T_CUBUK   0x1B313Au
#define T_CUBUK_YAZI 0xE8E2D0u

static uint32_t t_kar(uint32_t f, uint32_t b, uint32_t a) {
    uint32_t r = (((f >> 16) & 255) * a + ((b >> 16) & 255) * (255 - a) + 127) / 255;
    uint32_t g = (((f >> 8) & 255) * a + ((b >> 8) & 255) * (255 - a) + 127) / 255;
    uint32_t c = ((f & 255) * a + (b & 255) * (255 - a) + 127) / 255;
    return (r << 16) | (g << 8) | c;
}

/* yuzeyin sunulan karesinden (icerik koordinati) bir satir oku: UTF-8 */
int sinama_satir_ocr(int id, int x0, int y0, int hucre, uint32_t fg, uint32_t bg, char *o, int on);
static int satir_ocr(int id, int x0, int y0, int hucre, uint32_t fg, uint32_t bg, char *o, int on) {
    return sinama_satir_ocr(id, x0, y0, hucre, fg, bg, o, on);
}
int sinama_satir_ocr(int id, int x0, int y0, int hucre, uint32_t fg, uint32_t bg, char *o, int on) {
    int n = 0;
    ozkilit_al(&gorsel_kilit);
    const ulgen_yuzey_t *s = ulgen_yuzey_al(id);
    if (!s || !s->sunulan) { ozkilit_birak(&gorsel_kilit); o[0] = 0; return 0; }
    for (int h = 0; h < hucre; h++) {
        int x = x0 + h * YAZI_EN;
        if (x + YAZI_EN > s->w || y0 + YAZI_YUK > s->h) break;
        int bulunan = -1;
        for (int g = 0; g < YAZI_GLIF_SAYISI && bulunan < 0; g++) {
            int uy = 1;
            for (int r = 0; r < YAZI_YUK && uy; r++)
                for (int c = 0; c < YAZI_EN; c++) {
                    uint32_t a = yazi16[g][r * YAZI_EN + c];
                    uint32_t bek = a == 0 ? bg : a == 255 ? fg : t_kar(fg, bg, a);
                    if ((s->sunulan[(y0 + r) * s->w + x + c] & 0xFFFFFF) != bek) { uy = 0; break; }
                }
            if (uy) bulunan = g;
        }
        uint32_t k = bulunan < 0 ? '#' : yazi16_kod[bulunan];
        char u[4]; int ub;
        if (k < 0x80) { u[0] = (char)k; ub = 1; }
        else if (k < 0x800) { u[0] = (char)(0xC0 | (k >> 6)); u[1] = (char)(0x80 | (k & 63)); ub = 2; }
        else { u[0] = (char)(0xE0 | (k >> 12)); u[1] = (char)(0x80 | ((k >> 6) & 63)); u[2] = (char)(0x80 | (k & 63)); ub = 3; }
        if (n + ub + 1 >= on) break;
        for (int i = 0; i < ub; i++) o[n++] = u[i];
    }
    ozkilit_birak(&gorsel_kilit);
    while (n > 0 && o[n - 1] == ' ') n--;       /* sondaki bosluklar */
    o[n] = 0;
    return n;
}

/* 1.4.9: EKRANIN kendisinden (VRAM) bir satir oku: ust cubuk/menu gibi
 * birlestiricinin dogrudan cizdigi katmanlar icin. */
int sinama_ekran_ocr(int x0, int y0, int hucre, uint32_t fg, uint32_t bg, char *o, int on) {
    int n = 0;
    ozkilit_al(&gorsel_kilit);
    for (int h = 0; h < hucre; h++) {
        int x = x0 + h * YAZI_EN;
        int bulunan = -1;
        for (int g = 0; g < YAZI_GLIF_SAYISI && bulunan < 0; g++) {
            int uy = 1;
            for (int r = 0; r < YAZI_YUK && uy; r++)
                for (int c = 0; c < YAZI_EN; c++) {
                    uint32_t a = yazi16[g][r * YAZI_EN + c];
                    uint32_t bek = a == 0 ? bg : a == 255 ? fg : t_kar(fg, bg, a);
                    if ((fb_ekran_pikseli(x + c, y0 + r) & 0xFFFFFF) != bek) { uy = 0; break; }
                }
            if (uy) bulunan = g;
        }
        uint32_t k = bulunan < 0 ? '#' : yazi16_kod[bulunan];
        char u[4]; int ub;
        if (k < 0x80) { u[0] = (char)k; ub = 1; }
        else if (k < 0x800) { u[0] = (char)(0xC0 | (k >> 6)); u[1] = (char)(0x80 | (k & 63)); ub = 2; }
        else { u[0] = (char)(0xE0 | (k >> 12)); u[1] = (char)(0x80 | ((k >> 6) & 63)); u[2] = (char)(0x80 | (k & 63)); ub = 3; }
        if (n + ub + 1 >= on) break;
        for (int i = 0; i < ub; i++) o[n++] = u[i];
    }
    ozkilit_birak(&gorsel_kilit);
    while (n > 0 && o[n - 1] == ' ') n--;
    o[n] = 0;
    return n;
}

static int onek(const char *s, const char *on) {
    while (*on) { if (*s != *on) return 0; s++; on++; }
    return 1;
}
static int icerir(const char *s, const char *p) {
    for (; *s; s++) if (onek(s, p)) return 1;
    return 0;
}

static void t_tikla(int x, int y) {
    gui_fare_isle(x, y, 1);
    thread_sleep(1);
    gui_fare_isle(x, y, 0);
}

static void t_yaz(const char *m) { for (; *m; m++) keyboard_push_char(*m); }

static void tulpar_sinamasi(const char *sunucu, const char *port, const char *tls_port) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    int hata = 0;
    static char ocr[256], durum_ocr[256];
    /* ekrani UYGULAMA-TEST ile paylasmayalim: o bitsin */
    BEKLE(sinama148_bitti(), 120 * hz);
    /* 1) rihtimdaki TULPAR (tamga 4) */
    int rx = -1, ry = -1;
    for (int k = 0; k < 7 && rx < 0; k++) {
        int x, y, b;
        if (ulgen_theme_rihtim_konum(k, &x, &y, &b) < 0) continue;
        if (ulgen_theme_dock_hit(x + b / 2, y + b / 2) == 4) { rx = x + b / 2; ry = y + b / 2; }
    }
    if (rx >= 0 && ulgen_noktadaki_yuzey(rx, ry) < 0) t_tikla(rx, ry);
    BEKLE(uygulama_penceresi(UYG_TULPAR) >= 0, 10 * hz);
    int id = uygulama_penceresi(UYG_TULPAR);
    if (id < 0) hata |= 1;
    ocr[0] = 0;
    if (id >= 0) {
        BEKLE((satir_ocr(id, T_SOL, T_UST + 4, 26, T_BASLIK, T_KAGIT, ocr, sizeof ocr),
               onek(ocr, "TULPAR")), 5 * hz);
        if (!onek(ocr, "TULPAR")) hata |= 2;               /* yardim sayfasi cizildi */
    }
    serial_write("TULPAR-TEST: acilis sayfasi ilk satir='"); serial_write(ocr); serial_write("'\n");

    /* 2) adres cubuguna tikla, yaz, Enter */
    char hedef[80]; int n = 0;
    const char *on = "http://";
    while (*on) hedef[n++] = *on++;
    for (const char *p = sunucu; *p; p++) hedef[n++] = *p;
    hedef[n++] = ':';
    for (const char *p = port; *p; p++) hedef[n++] = *p;
    on = "/sayfa.html";
    while (*on) hedef[n++] = *on++;
    hedef[n] = 0;
    const ulgen_yuzey_t *ys = id >= 0 ? ulgen_yuzey_al(id) : 0;
    if (ys) {
        t_tikla(ys->x + 400, ys->y + 16);                 /* adres kutusu */
        thread_sleep(10);
        t_yaz(hedef);
        keyboard_push_char(13);
        BEKLE((satir_ocr(id, T_SOL, T_UST + 4, 20, T_BASLIK, T_KAGIT, ocr, sizeof ocr),
               onek(ocr, "Başlık Bir")), 15 * hz);
        if (!onek(ocr, "Başlık Bir")) hata |= 4;
        satir_ocr(id, 8, 640 - 22 + 3, 60, T_CUBUK_YAZI, T_CUBUK, durum_ocr, sizeof durum_ocr);
        if (!icerir(durum_ocr, "Ötüken Sınama")) hata |= 8;     /* <title> durum cubugunda */
        serial_write("TULPAR-TEST: sayfa ilk satir='"); serial_write(ocr);
        serial_write("' durum='"); serial_write(durum_ocr); serial_write("'\n");

        /* 3) klavyeyle ilk baglantiyi sec (->) ve ac (Enter): /kucuk.txt (duz metin) */
        keyboard_push_char((char)KLAVYE_SAG);
        thread_sleep(5);
        keyboard_push_char(13);
        BEKLE((satir_ocr(id, T_SOL, T_UST + 4, 26, T_YAZI, T_KAGIT, ocr, sizeof ocr),
               onek(ocr, "Ötüken yışı")), 15 * hz);
        if (!onek(ocr, "Ötüken yışı")) hata |= 16;
        serial_write("TULPAR-TEST: baglanti sonrasi ilk satir='"); serial_write(ocr); serial_write("'\n");

        /* 4) Backspace: geri */
        keyboard_push_char(8);
        BEKLE((satir_ocr(id, T_SOL, T_UST + 4, 20, T_BASLIK, T_KAGIT, ocr, sizeof ocr),
               onek(ocr, "Başlık Bir")), 15 * hz);
        if (!onek(ocr, "Başlık Bir")) hata |= 32;

        /* 5) HTTPS: sinama sertifikasi Mozilla koklerine baglanmaz -> TULPAR
         *    sayfayi ACMAMALI, hata sayfasi gostermeli (gecersiz sertifikayi
         *    kabul etme yolu yok) */
        if (tls_port && *tls_port) {
            n = 0; on = "https://";
            while (*on) hedef[n++] = *on++;
            for (const char *p = sunucu; *p; p++) hedef[n++] = *p;
            hedef[n++] = ':';
            for (const char *p = tls_port; *p; p++) hedef[n++] = *p;
            hedef[n++] = '/'; hedef[n] = 0;
            t_tikla(ys->x + 400, ys->y + 16);
            thread_sleep(10);
            t_yaz(hedef);
            keyboard_push_char(13);
            BEKLE((satir_ocr(id, T_SOL, T_UST + 4, 20, T_BASLIK, T_KAGIT, ocr, sizeof ocr),
                   onek(ocr, "Sayfa açılamadı")), 20 * hz);
            if (!onek(ocr, "Sayfa açılamadı")) hata |= 256;
            satir_ocr(id, 8, 640 - 22 + 3, 110, 0xD9534Fu, T_CUBUK, durum_ocr, sizeof durum_ocr);
            if (!onek(durum_ocr, "HTTPS reddedildi")) hata |= 512;
            serial_write("TULPAR-TEST: guvenilmeyen HTTPS ilk satir='"); serial_write(ocr);
            serial_write("' durum='"); serial_write(durum_ocr); serial_write("'\n");
        } else hata |= 256;

        /* 6) kapat dugmesi: surec biter, baglanti tutamagi kalmaz */
        int kx = ys->x + ys->w - 10, ky = ys->y - ULGEN_BASLIK_YUK / 2;
        if (ulgen_kapat_dugmesi_mi(id, kx, ky)) t_tikla(kx, ky);
        BEKLE(uygulama_pid(UYG_TULPAR) == 0, 10 * hz);
        if (uygulama_pid(UYG_TULPAR) != 0) hata |= 64;
        BEKLE(ag_tutamak_sayisi() == 0, 5 * hz);
        if (ag_tutamak_sayisi() != 0) hata |= 128;
    }
    serial_write("TULPAR-TEST: rihtim -> adres yaz -> sayfa (OCR) -> baglanti -> geri -> kapat hata=");
    yaz_sayi((uint64_t)hata);
    serial_write(hata ? " -> HATA\n" : " -> BASARILI\n");
}

void sinama149_ag_baslat(void) { thread_create("AgSinama", gorev); }


/* ===================== KERVAN-AG-TEST (1.4.9) =====================
 * KERVAN penceresinde "Adres" alanina yaz, Enter: paket AGDAN indirilir ve
 * cekirdekte dogrulanip kurulur. Once BOZUK paket (bir bayti degismis):
 * reddedilmeli ve diske hicbir sey yazilmamali. Sonra iyi paket: kurulur,
 * icindeki MERHABA.ELF Uygulamalar menusunde "Kurulu paketler" altinda
 * gorunur (ekrandan OCR), tiklaninca EN DAR paket profiliyle acilir.
 * Kaldirilinca menuden duser. Sunucu: tests/ag/sunucu.py (/paket.krv, /bozuk.krv) */
static void kervan_ag_sinamasi(const char *sunucu, const char *port) {
    uint32_t hz = timer_hz() ? timer_hz() : 100;
    int hata = 0;
    char url[96], s[160];
    /* OCR gorsel kilidi tutar: her tikte denemek uygulamanin cizmesini
     * aclikta birakiyordu (tuslar yavas islendi). Ceyrek saniyede bir. */
    #define BEKLE_SEYREK(kosul, tik) do { uint64_t _t0 = timer_get_ticks(); \
        while (!(kosul) && timer_get_ticks() - _t0 < (tik)) thread_sleep(hz / 4); } while (0)
    #define K_ARKA 0x14232Bu
    #define K_VURGU 0x4FB3A9u
    #define K_KIRMIZI 0xD9534Fu
    BEKLE(uygulama_pid(UYG_TULPAR) == 0, 10 * hz);
    uygulama_ac(UYG_KERVAN);
    BEKLE(uygulama_penceresi(UYG_KERVAN) >= 0, 10 * hz);
    int id = uygulama_penceresi(UYG_KERVAN);
    const ulgen_yuzey_t *ys = id >= 0 ? ulgen_yuzey_al(id) : 0;
    uint32_t bos0 = fat16_bos_kume_sayisi();
    if (!ys) hata |= 1;
    else {
        int by = 470 - 100 + 8;
        /* 1) bozuk paket */
        url[0] = 0;
        const char *p1 = "http://"; int n = 0;
        while (*p1) url[n++] = *p1++;
        for (const char *q = sunucu; *q; q++) url[n++] = *q;
        url[n++] = ':';
        for (const char *q = port; *q; q++) url[n++] = *q;
        int taban = n;
        const char *b1 = "/bozuk.krv"; while (*b1) url[n++] = *b1++;
        url[n] = 0;
        thread_sleep(hz / 2);
        t_tikla(ys->x + 200, ys->y + by + 44);
        thread_sleep(hz / 5);
        t_yaz(url); keyboard_push_char('\r');
        BEKLE_SEYREK((satir_ocr(id, 14, by + 66, 60, K_KIRMIZI, K_ARKA, s, sizeof s), onek(s, "KURULMADI")), 20 * hz);
        serial_write("KERVAN-AG-TEST:   bozuk paket (OCR) '"); serial_write(s); serial_write("'\n");
        if (!onek(s, "KURULMADI: IMZA GECERSIZ")) hata |= 2;
        if (kervan2_kurulu_mu("merhaba-paketi", 0) || fat16_bos_kume_sayisi() != bos0) hata |= 2;
        /* 2) iyi paket: alanı temizle, yeni adres */
        t_tikla(ys->x + 200, ys->y + by + 44);
        thread_sleep(hz / 5);
        /* klavye kuyrugu sinirli: tuslar parca parca (uygulama okuyabilsin) */
        for (int i = 0; i < n; i++) { keyboard_push_char(0x08); thread_sleep(2); }
        thread_sleep(hz / 5);
        n = taban;
        const char *b2 = "/paket.krv"; while (*b2) url[n++] = *b2++;
        url[n] = 0;
        for (int i = 0; url[i]; i++) { keyboard_push_char(url[i]); thread_sleep(2); }
        thread_sleep(hz / 10);
        keyboard_push_char('\r');
        BEKLE_SEYREK((satir_ocr(id, 14, by + 66, 60, K_VURGU, K_ARKA, s, sizeof s), onek(s, "İndirildi ve kuruldu")), 20 * hz);
        serial_write("KERVAN-AG-TEST:   iyi paket (OCR) '"); serial_write(s); serial_write("'\n");
        if (!onek(s, "İndirildi")) { char k2[160]; satir_ocr(id, 14, by + 66, 60, K_KIRMIZI, K_ARKA, k2, sizeof k2);
            serial_write("KERVAN-AG-TEST:   (kirmizi satir) '"); serial_write(k2); serial_write("'\n");
            satir_ocr(id, 76, by + 38, 60, 0x10181Cu, 0xFFFFFFu, k2, sizeof k2); serial_write("KERVAN-AG-TEST:   (adres) '"); serial_write(k2); serial_write("'\n");
            satir_ocr(id, 76, by + 38, 60, 0xE8E2D0u, 0x0F1E25u, k2, sizeof k2); serial_write("KERVAN-AG-TEST:   (adres2) '"); serial_write(k2); serial_write("'\n"); }
        if (!onek(s, "İndirildi ve kuruldu (imza doğrulandı).")) hata |= 4;
        if (!kervan2_kurulu_mu("merhaba-paketi", 0)) hata |= 4;
        int kx = ys->x + ys->w - 10, ky = ys->y - ULGEN_BASLIK_YUK / 2;
        if (ulgen_kapat_dugmesi_mi(id, kx, ky)) t_tikla(kx, ky);
        BEKLE(uygulama_pid(UYG_KERVAN) == 0, 10 * hz);
    }
    /* 3) menü: "Kurulu paketler" satiri (ekrandan) ve acilis */
    char dosya[13]; dosya[0] = 0;
    if (kervan2_uygulama_sayisi() != 1 || kervan2_uygulama_al(0, dosya, 0) != 0) hata |= 8;
    int ux, uy, uw, uh;
    ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
    t_tikla(ux + 30, ULGEN_UST_BAR / 2);
    thread_sleep(2);
    const tema_t *tm = tema_simdiki();
    int satir = 15 + 1;                       /* 15 tamga + "KURULU PAKETLER" ayraci */
    BEKLE_SEYREK((sinama_ekran_ocr(ux + 14, uy + 4 + satir * 26 + 5, 12, tm->gorev_yazi, tm->gorev_cubugu, s, sizeof s),
           onek(s, "MERHABA.ELF")), hz);
    serial_write("KERVAN-AG-TEST:   menu satiri (ekran) '"); serial_write(s); serial_write("'\n");
    if (!onek(s, "MERHABA.ELF")) hata |= 8;
    t_tikla(ux + 40, uy + 4 + satir * 26 + 13);
    int pid_u = UYG_SAYI;                      /* ilk paket yuvasi */
    BEKLE(uygulama_penceresi(pid_u) >= 0, 10 * hz);
    int pid2 = uygulama_penceresi(pid_u);
    if (pid2 < 0) hata |= 16;
    else {
        BEKLE_SEYREK((satir_ocr(pid2, 14, 7, 30, 0x4FB3A9u, 0x1B313Au, s, sizeof s), onek(s, "Merhaba — KERVAN paketi")), 5 * hz);
        serial_write("KERVAN-AG-TEST:   paket uygulamasi baslik (OCR) '"); serial_write(s); serial_write("'\n");
        if (!onek(s, "Merhaba — KERVAN paketi")) hata |= 16;
        if (tore_get_capabilities(uygulama_gorev(pid_u)) != TORE_PROFILE_PAKET_UYG) hata |= 32;
        const ulgen_yuzey_t *y2 = ulgen_yuzey_al(pid2);
        if (y2) { int kx = y2->x + y2->w - 10, ky = y2->y - ULGEN_BASLIK_YUK / 2; if (ulgen_kapat_dugmesi_mi(pid2, kx, ky)) t_tikla(kx, ky); }
        BEKLE(uygulama_pid(pid_u) == 0, 10 * hz);
        if (uygulama_pid(pid_u) != 0) hata |= 16;
    }
    /* 4) kaldirinca menuden duser */
    char neden[64];
    if (kervan2_kaldir("merhaba-paketi", neden) != 0 || kervan2_uygulama_sayisi() != 0) hata |= 64;
    serial_write("KERVAN-AG-TEST: agdan indir (bozuk red, iyi kur), menude paket uygulamasi, dar profil, kaldir hata=");
    yaz_sayi((uint64_t)hata);
    serial_write(hata ? " -> HATA\n" : " -> BASARILI\n");
}
