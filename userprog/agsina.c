/* =====================================================================
 * AGSINA — ULAK ağ yığınının Ring3'ten uçtan uca sınaması (1.4.9)
 * Yalnız sınama diskinde bulunur (tests/ag/calistir.sh ekler).
 * Karşı uç: tests/ag/sunucu.py (QEMU slirp 10.0.2.2). Beklenen boy ve
 * FNV-1a özetleri /AGSINA.CFG'den okunur; içerik bayt bayt denetlenir.
 * ===================================================================== */
#include "http.h"
#include "bearssl.h"
extern const br_x509_trust_anchor SINAMA_KOKLER[1];
void tls_kokleri_ayarla(const br_x509_trust_anchor *k, unsigned n);

static uint8_u govde[2 * 1024 * 1024];
static int hata_say = 0;
static char s[256];

static uint32_u fnv(const uint8_u *p, uint32_u n) {
    uint32_u h = 0x811C9DC5u;
    for (uint32_u i = 0; i < n; i++) { h ^= p[i]; h *= 0x01000193u; }
    return h;
}
static void yaz(const char *a) { puts(a); }
static void sayi(const char *on, uint64_u v) {
    char t[24]; sayi_yaz(t, v, 1); s[0] = 0;
    dize_ekle(s, sizeof s, on); dize_ekle(s, sizeof s, t); yaz(s);
}
static void onalti(uint32_u v) {
    char t[9]; for (int i = 0; i < 8; i++) t[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15];
    t[8] = 0; yaz(t);
}
static void denetle(const char *ad, int kosul) {
    s[0] = 0;
    dize_ekle(s, sizeof s, "AGSINA: ");
    dize_ekle(s, sizeof s, ad);
    dize_ekle(s, sizeof s, kosul ? " -> GECTI\n" : " -> KALDI\n");
    yaz(s);
    if (!kosul) hata_say++;
}

static int icerir_s(const char *m, const char *p) {
    for (; *m; m++) {
        const char *a = m, *b = p;
        while (*b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

static const char *sayi_oku(const char *p, uint32_u *v, int taban) {
    while (*p == ' ') p++;
    uint32_u x = 0;
    for (;; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (taban == 16 && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        x = x * (uint32_u)taban + (uint32_u)d;
    }
    *v = x;
    return p;
}

static http_yanit_t y;
static char url[200];
static char sunucu[20] = "10.0.2.2";
static uint8_u sip[4];

static void url_kur(const char *yol, uint32_u port) {
    char t[12];
    url[0] = 0;
    dize_ekle(url, sizeof url, "http://");
    dize_ekle(url, sizeof url, sunucu);
    dize_ekle(url, sizeof url, ":");
    sayi_yaz(t, port, 1); dize_ekle(url, sizeof url, t);
    dize_ekle(url, sizeof url, yol);
}

int main(void) {
    static char cfg[320];
    int fd = open("/AGSINA.CFG");
    int n = fd >= 0 ? (int)read(fd, cfg, sizeof cfg - 1) : -1;
    if (fd >= 0) close(fd);
    if (n <= 0) { yaz("AGSINA: CFG okunamadi\nAGSINA SONUC: KALDI\n"); return 1; }
    cfg[n] = 0;
    uint32_u port, kapali, bboy, bfnv, pboy, pfnv, kfnv;
    const char *p = cfg;
    {   /* sunucu adresi (slirp: 10.0.2.2; TAP: 10.9.0.1) */
        int i = 0;
        while (*p && *p != ' ' && i < 19) { sunucu[i++] = *p++; }
        sunucu[i] = 0;
    }
    p = sayi_oku(p, &port, 10); p = sayi_oku(p, &kapali, 10);
    p = sayi_oku(p, &bboy, 10); p = sayi_oku(p, &bfnv, 16);
    p = sayi_oku(p, &pboy, 10); p = sayi_oku(p, &pfnv, 16);
    p = sayi_oku(p, &kfnv, 16);
    uint32_u tp[5] = {0, 0, 0, 0, 0};
    while (*p && *p != 'T') p++;
    if (p[0] == 'T' && p[1] == 'L' && p[2] == 'S') {
        p += 3;
        for (int i = 0; i < 5; i++) p = sayi_oku(p, &tp[i], 10);
    }

    ag_bilgi_u b;
    denetle("ag bilgisi: DHCP ile yapilandi", ag_bilgi(&b) == 0 && b.yapilandi && b.ip[0] == 10);

    uint8_u ip[4];
    denetle("noktali IP DNS'siz cozulur", ag_coz(sunucu, sip) == 0 && sip[0] == 10);
    denetle("olmayan ad cozulemez (.invalid)", ag_coz("yok-boyle-bir-ad.invalid", ip) != 0);

    /* kapalı port: RST ile hemen reddedilmeli (8 sn beklemeden) */
    long t0 = ticks();
    int kt = ag_baglan(sip, (int)kapali);
    long gecen = ticks() - t0;
    denetle("kapali porta baglanti reddedildi", kt < 0);
    denetle("red hizli geldi (< 3 sn)", gecen < 3 * (long)sistem_bil(SYSBIL_TIK_HZ));

    url_kur("/kucuk.txt", port);
    int r = http_al(url, &y, govde, sizeof govde, 0, 0);
    denetle("kucuk dosya (Content-Length)", r == 0 && y.durum == 200 && fnv(govde, y.govde_boy) == kfnv);

    url_kur("/buyuk.bin", port);
    r = http_al(url, &y, govde, sizeof govde, 0, 0);
    uint32_u hz = (uint32_u)sistem_bil(SYSBIL_TIK_HZ);
    denetle("1 MB dosya: boy ve ozet birebir", r == 0 && y.durum == 200 && !y.kesik &&
            y.govde_boy == bboy && fnv(govde, y.govde_boy) == bfnv);
    if (y.sure_tik) {
        sayi("AGSINA: 1 MB hiz (KB/sn) = ", (uint64_u)y.govde_boy * hz / y.sure_tik / 1024);
        yaz("\n");
    }
    if (y.govde_boy != bboy || fnv(govde, y.govde_boy) != bfnv) {
        sayi("AGSINA:   alinan boy=", y.govde_boy); yaz(" ozet="); onalti(fnv(govde, y.govde_boy));
        yaz(" hata="); yaz(y.hata); yaz("\n");
    }

    url_kur("/parcali", port);
    r = http_al(url, &y, govde, sizeof govde, 0, 0);
    denetle("parcali aktarim (chunked)", r == 0 && y.govde_boy == pboy && fnv(govde, y.govde_boy) == pfnv);

    url_kur("/yonlen", port);
    r = http_al(url, &y, govde, sizeof govde, 0, 0);
    denetle("302 yonlendirmesi izlendi", r == 0 && y.durum == 200 && fnv(govde, y.govde_boy) == kfnv);

    url_kur("/eof", port);
    r = http_al(url, &y, govde, sizeof govde, 0, 0);
    denetle("uzunluksuz yanit (kapanisa dek)", r == 0 && y.govde_boy == 200000);

    url_kur("/yok", port);
    r = http_al(url, &y, govde, sizeof govde, 0, 0);
    denetle("404 durumu dogru iletildi", r == 0 && y.durum == 404);

    /* tampon siniri: 1 MB dosya 100 KB tampona -> kesik, tasma yok */
    url_kur("/buyuk.bin", port);
    govde[100000] = 0xA5;
    r = http_al(url, &y, govde, 100000, 0, 0);
    denetle("kucuk tampon: kesik isaretlendi, tasmadi", r == 0 && y.kesik && y.govde_boy == 100000 &&
            govde[100000] == 0xA5);

    /* eszamanli uc baglanti */
    int t[3];
    int acildi = 1;
    for (int i = 0; i < 3; i++) { t[i] = ag_baglan(sip, (int)port); if (t[i] < 0) acildi = 0; }
    denetle("uc eszamanli baglanti", acildi);
    if (acildi) {
        const char *ist = "GET /kucuk.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
        for (int i = 0; i < 3; i++) ag_gonder_tum(t[i], ist, (int)strlen(ist), 500);
        static uint8_u tb[3][4096];
        int boy[3] = {0, 0, 0}, bitti[3] = {0, 0, 0};
        long bas = ticks();
        while ((!bitti[0] || !bitti[1] || !bitti[2]) && ticks() - bas < 10 * (long)hz) {
            for (int i = 0; i < 3; i++) {
                if (bitti[i]) continue;
                int k = ag_al(t[i], tb[i] + boy[i], 4096 - boy[i]);
                if (k > 0) boy[i] += k; else if (k != 0) bitti[i] = 1;
            }
            sleep_ticks(1);
        }
        int ayni = bitti[0] && bitti[1] && bitti[2] && boy[0] > 0 && boy[0] == boy[1] && boy[1] == boy[2];
        denetle("uc yanit ayri ve tam geldi", ayni);
        for (int i = 0; i < 3; i++) ag_kapat(t[i]);
    }

    /* yabanci tutamak: olmayan tutamaga yazmak reddedilir */
    denetle("gecersiz tutamak reddedildi", ag_gonder(31, "x", 1) < 0 && ag_al(-1, govde, 10) < 0);

    /* ---------------- TLS (HTTPS) ---------------- */
    if (tp[0]) {
        http_ad_esle("tulpar.sinama", sip);
        static char tu[200];
#define TURL(port, yol) do { char _t[12]; tu[0] = 0; dize_ekle(tu, sizeof tu, "https://tulpar.sinama:"); \
        sayi_yaz(_t, port, 1); dize_ekle(tu, sizeof tu, _t); dize_ekle(tu, sizeof tu, yol); } while (0)
        /* 1) varsayılan (Mozilla) kökler sınama sertifikasına GÜVENMEMELİ */
        TURL(tp[0], "/kucuk.txt");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: Mozilla kokleriyle sinama sertifikasi REDDEDILDI", r != 0 && y.durum == 0 && icerir_s(y.hata, "köke"));
        yaz("AGSINA:   neden: "); yaz(y.hata); yaz("\n");
        tls_kokleri_ayarla(SINAMA_KOKLER, 1);
        /* 2) geçerli RSA sertifika */
        TURL(tp[0], "/kucuk.txt");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: gecerli sertifika (RSA) kucuk dosya", r == 0 && y.durum == 200 && fnv(govde, y.govde_boy) == kfnv);
        if (r != 0) { yaz("AGSINA:   hata: "); yaz(y.hata); yaz("\n"); }
        TURL(tp[0], "/buyuk.bin");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: 1 MB bayt bayt dogru", r == 0 && y.govde_boy == bboy && fnv(govde, y.govde_boy) == bfnv);
        if (y.sure_tik) { sayi("AGSINA: TLS 1 MB hiz (KB/sn) = ", (uint64_u)y.govde_boy * hz / y.sure_tik / 1024); yaz("\n"); }
        TURL(tp[0], "/parcali");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: parcali aktarim", r == 0 && y.govde_boy == pboy && fnv(govde, y.govde_boy) == pfnv);
        TURL(tp[0], "/eof");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: uzunluksuz yanit (close_notify)", r == 0 && y.govde_boy == 200000);
        /* 3) ECDSA sertifika */
        TURL(tp[1], "/kucuk.txt");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: gecerli sertifika (ECDSA P-256)", r == 0 && fnv(govde, y.govde_boy) == kfnv);
        /* 4) yanlış ad, süresi geçmiş, yabancı kök: HEPSİ reddedilmeli */
        TURL(tp[2], "/kucuk.txt");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: baska ada verilmis sertifika reddedildi", r != 0 && icerir_s(y.hata, "bu ada"));
        yaz("AGSINA:   neden: "); yaz(y.hata); yaz("\n");
        TURL(tp[3], "/kucuk.txt");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: suresi gecmis sertifika reddedildi", r != 0 && icerir_s(y.hata, "süresi"));
        yaz("AGSINA:   neden: "); yaz(y.hata); yaz("\n");
        TURL(tp[4], "/kucuk.txt");
        r = http_al(tu, &y, govde, sizeof govde, 0, 0);
        denetle("TLS: bilinmeyen kokun sertifikasi reddedildi", r != 0 && icerir_s(y.hata, "köke"));
        yaz("AGSINA:   neden: "); yaz(y.hata); yaz("\n");
        /* 5) düz HTTP portuna TLS: el sıkışması başarısız, askıda kalmaz */
        long t1 = ticks();
        {
            char u2[64]; char t[12]; u2[0] = 0;
            dize_ekle(u2, sizeof u2, "https://tulpar.sinama:"); sayi_yaz(t, port, 1); dize_ekle(u2, sizeof u2, t);
            dize_ekle(u2, sizeof u2, "/kucuk.txt");
            r = http_al(u2, &y, govde, sizeof govde, 0, 0);
        }
        denetle("TLS: duz HTTP sunucusuna TLS -> hata, takilmadi", r != 0 && ticks() - t1 < 25 * (long)hz);
    } else {
        denetle("TLS sinama portlari CFG'de yok", 0);
    }

    s[0] = 0;
    dize_ekle(s, sizeof s, hata_say ? "AGSINA SONUC: KALDI\n" : "AGSINA SONUC: GECTI\n");
    yaz(s);

    /* son: bir baglanti ACIK birakilip cikilir -> reap kapatmali (cekirdek sinar) */
    int acik = ag_baglan(sip, (int)port);
    sayi("AGSINA: acik birakilan tutamak=", (uint64_u)(acik < 0 ? 999 : acik));
    yaz("\n");
    return 0;
}
