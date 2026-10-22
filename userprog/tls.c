/* =====================================================================
 * TULPAR — TLS katmanı (1.4.9): BearSSL 0.6 (MIT, T. Pornin) üzerinde.
 *
 * Kriptografi KENDİMİZ YAZMADIK: kendi yazılmış TLS kriptosu güvenlik
 * açığı kaynağıdır. BearSSL gömülü dizgeler için tasarlanmıştır: malloc
 * ve libc istemez, sabit zamanlı (ct) uygulamalar seçildi, SSE yok.
 *   - kök sertifikalar: Mozilla (Ubuntu ca-certificates), ta_kokler.c
 *   - saat: çekirdeğin CMOS RTC okuması (SYS_ZAMAN) — sertifika tarihi
 *   - rastgelelik: RDSEED/RDRAND (SYS_RASTGELE); yoksa HTTPS REDDEDİLİR
 *   - TLS 1.0–1.2 (BearSSL'de 1.3 yok), SNI gönderilir
 * Tek bağlam: aynı anda tek HTTPS bağlantısı (TULPAR sırayla indirir).
 * ===================================================================== */
#include "http.h"
#include "bearssl.h"

extern const br_x509_trust_anchor TULPAR_KOKLER[];
extern const unsigned TULPAR_KOK_SAYISI;

static br_ssl_client_context sc;
static br_x509_minimal_context xc;
static unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
static br_sslio_context ioc;
static int tls_eof;

/* güven kökleri: varsayılan Mozilla listesi. Sınama programı (AGSINA)
 * kendi sınama kökünü koyar; TULPAR bunu ÇAĞIRMAZ. */
static const br_x509_trust_anchor *kokler = 0;
static unsigned kok_sayisi = 0;
void tls_kokleri_ayarla(const br_x509_trust_anchor *k, unsigned n) { kokler = k; kok_sayisi = n; }

static long hz(void) { long h = (long)sistem_bil(SYSBIL_TIK_HZ); return h > 0 ? h : 100; }

static int dusuk_oku(void *ctx, unsigned char *buf, size_t len) {
    akis_t *a = (akis_t *)ctx;
    long bas = ticks();
    if (len > 4096) len = 4096;
    for (;;) {
        int n = ag_al(a->t, buf, (int)len);
        if (n > 0) return n;
        if (n == AG_SON) { tls_eof = 1; return -1; }
        if (n < 0) return -1;
        if (ticks() - bas > 20 * hz()) return -1;
        sleep_ticks(1);
    }
}

static int dusuk_yaz(void *ctx, const unsigned char *buf, size_t len) {
    akis_t *a = (akis_t *)ctx;
    long bas = ticks();
    if (len > 4096) len = 4096;
    for (;;) {
        int n = ag_gonder(a->t, buf, (int)len);
        if (n > 0) return n;
        if (n < 0) return -1;
        if (ticks() - bas > 20 * hz()) return -1;
        sleep_ticks(1);
    }
}

/* proleptik Gregoryen: 0000-01-01'den bu yana gün (BearSSL'in beklediği) */
static uint32_u gun_sayisi(int y, int m, int d) {
    /* H. Hinnant'ın days_from_civil'i (1970 yerine 0000 tabanı) */
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long gun1970 = era * 146097 + doe - 719468;
    return (uint32_u)(gun1970 + 719528);          /* 0000-01-01 -> 1970-01-01 = 719528 gün */
}

static void hata_koy(char *h, int hb, const char *a, int kod) {
    h[0] = 0;
    dize_ekle(h, hb, a);
    if (kod) {
        char t[12]; sayi_yaz(t, (uint64_u)kod, 1);
        dize_ekle(h, hb, " (kod "); dize_ekle(h, hb, t); dize_ekle(h, hb, ")");
    }
}

static void hata_acikla(int e, char *h, int hb) {
    switch (e) {
    case BR_ERR_X509_NOT_TRUSTED:
        hata_koy(h, hb, "HTTPS reddedildi: sertifika güvenilen bir köke bağlanmıyor", 0); break;
    case BR_ERR_X509_EXPIRED:
        hata_koy(h, hb, "HTTPS reddedildi: sertifikanın süresi dolmuş ya da henüz geçerli değil (saat doğru mu?)", 0); break;
    case BR_ERR_X509_BAD_SERVER_NAME:
        hata_koy(h, hb, "HTTPS reddedildi: sertifika bu ada verilmemiş", 0); break;
    case BR_ERR_X509_NOT_CA: case BR_ERR_X509_BAD_SIGNATURE: case BR_ERR_X509_WEAK_PUBLIC_KEY:
        hata_koy(h, hb, "HTTPS reddedildi: sertifika zinciri geçersiz", e); break;
    case BR_ERR_BAD_VERSION: case BR_ERR_BAD_CIPHER_SUITE: case BR_ERR_UNSUPPORTED_VERSION:
        hata_koy(h, hb, "HTTPS kurulamadı: sunucu TLS 1.2 ile uyuşmadı", e); break;
    case BR_ERR_IO:
        hata_koy(h, hb, "HTTPS kurulamadı: bağlantı koptu ya da zaman aşımı", 0); break;
    default:
        hata_koy(h, hb, "HTTPS el sıkışması başarısız", e); break;
    }
}

/* SYS_RASTGELE / SYS_ZAMAN */
#define SYS_RASTGELE 43
#define SYS_ZAMAN    44
typedef struct { uint32_u yil, ay, gun, saat, dakika, saniye; } zaman_u;

int guvenli_ac(akis_t *a, const char *host, char *hata, int hboy) {
    unsigned char ent[64];
    if (syscall(SYS_RASTGELE, (long)ent, sizeof ent, 0, 0) != (long)sizeof ent) {
        hata_koy(hata, hboy, "HTTPS kurulamadı: donanım rastgele sayı üreteci (RDRAND/RDSEED) yok", 0);
        return -1;
    }
    zaman_u z;
    if (syscall(SYS_ZAMAN, (long)&z, 0, 0, 0) != 0 || z.yil < 2024 || z.yil > 2200) {
        hata_koy(hata, hboy, "HTTPS kurulamadı: saat okunamadı (sertifika tarihi denetlenemez)", 0);
        return -1;
    }
    if (!kokler) { kokler = TULPAR_KOKLER; kok_sayisi = TULPAR_KOK_SAYISI; }
    br_ssl_client_init_full(&sc, &xc, kokler, kok_sayisi);
    br_x509_minimal_set_time(&xc, gun_sayisi((int)z.yil, (int)z.ay, (int)z.gun),
                             z.saat * 3600u + z.dakika * 60u + z.saniye);
    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);
    br_ssl_engine_inject_entropy(&sc.eng, ent, sizeof ent);
    memset(ent, 0, sizeof ent);
    if (!br_ssl_client_reset(&sc, host, 0)) {
        hata_acikla(br_ssl_engine_last_error(&sc.eng), hata, hboy);
        return -1;
    }
    tls_eof = 0;
    /* el sıkışmasını burada bitir: hata sayfa isteğinden önce, açık nedenle */
    for (;;) {
        unsigned st = br_ssl_engine_current_state(&sc.eng);
        if (st & BR_SSL_CLOSED) {
            int e = br_ssl_engine_last_error(&sc.eng);
            hata_acikla(e ? e : BR_ERR_IO, hata, hboy);
            return -1;
        }
        if (st & BR_SSL_SENDREC) {
            size_t len;
            unsigned char *buf = br_ssl_engine_sendrec_buf(&sc.eng, &len);
            int w = dusuk_yaz(a, buf, len);
            if (w < 0) { br_ssl_engine_close(&sc.eng); hata_acikla(BR_ERR_IO, hata, hboy); return -1; }
            br_ssl_engine_sendrec_ack(&sc.eng, (size_t)w);
            continue;
        }
        if (st & BR_SSL_SENDAPP) break;                 /* el sıkışması tamam */
        if (st & BR_SSL_RECVREC) {
            size_t len;
            unsigned char *buf = br_ssl_engine_recvrec_buf(&sc.eng, &len);
            int r = dusuk_oku(a, buf, len);
            if (r < 0) {
                /* karşı taraf kapattı: motorun kendi hatası (ör. alert) daha anlamlı */
                int e = br_ssl_engine_last_error(&sc.eng);
                hata_acikla(e ? e : BR_ERR_IO, hata, hboy);
                return -1;
            }
            br_ssl_engine_recvrec_ack(&sc.eng, (size_t)r);
            continue;
        }
    }
    br_sslio_init(&ioc, &sc.eng, dusuk_oku, a, dusuk_yaz, a);
    a->tls = &ioc;
    return 0;
}

int guvenli_yaz(akis_t *a, const void *v, int n) {
    (void)a;
    if (br_sslio_write_all(&ioc, v, (size_t)n) != 0) return -1;
    return br_sslio_flush(&ioc) == 0 ? 0 : -1;
}

int guvenli_oku(akis_t *a, void *v, int n) {
    (void)a;
    int r = br_sslio_read(&ioc, v, (size_t)n);
    if (r > 0) return r;
    int e = br_ssl_engine_last_error(&sc.eng);
    /* close_notify ile kapanış (e==0) ya da close_notify'sız TCP kapanışı:
     * ikisi de akış sonu. Gövdenin eksik kalıp kalmadığını HTTP katmanı
     * (Content-Length / parçalı aktarım) ayrıca denetler. */
    if (e == 0 || (tls_eof && e == BR_ERR_IO)) return AG_SON;
    return -1;
}

void guvenli_kapat(akis_t *a) {
    (void)a;
    br_ssl_engine_close(&sc.eng);         /* close_notify kuyruğa; beklemeden TCP kapanır */
    a->tls = 0;
}
