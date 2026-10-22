/* =====================================================================
 * TAMGA — dijital kimlik (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM | DISK_OKU (dosya mührü için yalnız okuma).
 * Gösterir: çekirdeğin kimliği (kod ve salt okunur verinin SHA-256'sı),
 * KERVAN/ERGENEKON imza anahtarlarının parmak izleri, dizge imajını imzalayan
 * anahtar, TULPAR'ın güven listesi. "Dosya mührü": bir dosyanın SHA-256'sı.
 * ===================================================================== */
#include "uyg.h"
#include "bearssl.h"

extern const br_x509_trust_anchor TULPAR_KOKLER[];
extern const unsigned TULPAR_KOK_SAYISI;

#define GEN 720
#define YUK 470
static int pen;
static char dosya[40] = "UMAY.ELF";
static int yaziyor;
static char muhur[72], muhur_not[80];
static uint32_u muhur_renk = R_SONUK;
static int muhur_y;                /* çizimdeki satırın yeri (tıklama için) */

static void hex(char *d, const uint8_u *v, int n) {
    for (int i = 0; i < n; i++) { d[2 * i] = "0123456789abcdef"[v[i] >> 4]; d[2 * i + 1] = "0123456789abcdef"[v[i] & 15]; }
    d[2 * n] = 0;
}

static void muhur_hesapla(void) {
    char yol[48]; yol[0] = '/'; yol[1] = 0; dize_ekle(yol, sizeof yol, dosya);
    muhur[0] = 0; muhur_not[0] = 0;
    int fd = open(yol);
    if (fd < 0) { dize_ekle(muhur_not, sizeof muhur_not, "dosya yok ya da açılamadı"); muhur_renk = R_KIRMIZI; return; }
    static uint8_u t[4096];
    br_sha256_context h; br_sha256_init(&h);
    uint64_u n = 0;
    for (;;) { long r = read(fd, t, sizeof t); if (r <= 0) break; br_sha256_update(&h, t, (size_t)r); n += (uint64_u)r; }
    close(fd);
    uint8_u o[32]; br_sha256_out(&h, o);
    hex(muhur, o, 32);
    char s[24]; sayi_yaz(s, n, 1);
    dize_ekle(muhur_not, sizeof muhur_not, s); dize_ekle(muhur_not, sizeof muhur_not, " bayt");
    muhur_renk = R_YAZI;
}

static void satir(int *y, const char *baslik, const char *deger, uint32_u r, const char *not) {
    pencere_yazi(pen, 16, *y, baslik, R_SONUK); *y += 18;
    pencere_yazi(pen, 30, *y, deger, r); *y += 18;
    if (not && *not) { pencere_yazi(pen, 30, *y, not, R_SONUK); *y += 18; }
    *y += 6;
}

static void ciz(void) {
    char s[140], h[72], t[16];
    tamga_u tb;
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "TAMGA — Dijital Kimlik", R_VURGU);
    int y = 42;
    if (syscall(SYS_TAMGA_BILGI, 1, 0, (long)&tb, 0) == 0) {
        hex(h, tb.ozet, 32);
        s[0] = 0; sayi_yaz(t, tb.boy, 1); dize_ekle(s, sizeof s, "kod + salt okunur veri, "); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " bayt");
        satir(&y, "Çekirdek özeti (SHA-256)", h, R_YAZI, s);
    }
    for (int k = 0; k < 2; k++) {
        if (syscall(SYS_TAMGA_BILGI, 0, k, (long)&tb, 0) != 0) continue;
        hex(h, tb.ozet, 32);
        satir(&y, k == 0 ? "KERVAN / ERGENEKON üretim anahtarı (parmak izi)" : "Sınama anahtarı (parmak izi)",
              h, k == 0 ? R_YAZI : R_SONUK,
              tb.gecerli ? (k == 0 ? "bu yapıda geçerli" : "bu yapıda GEÇERLİ (tanı yapısı)") : "bu yapıda geçersiz: bu anahtarla imzalı paket reddedilir");
    }
    ergenekon_u e;
    s[0] = 0;
    if (syscall(SYS_ERGENEKON, 0, (long)&e, 0, 0) == 0 && e.var) {
        if (e.gecerli) {
            dize_ekle(s, sizeof s, e.ad); dize_ekle(s, sizeof s, " "); dize_ekle(s, sizeof s, e.surum);
            dize_ekle(s, sizeof s, e.anahtar_no == 0 ? " · üretim anahtarıyla imzalı" : " · sınama anahtarıyla imzalı");
        } else { dize_ekle(s, sizeof s, "GEÇERSİZ: "); dize_ekle(s, sizeof s, e.neden); }
    } else dize_ekle(s, sizeof s, "yok");
    satir(&y, "Dizge imajı (ERGENEKON)", s, e.var && e.gecerli ? R_VURGU : R_UYARI, 0);
    {   /* TULPAR güven listesi: kök adlarının ve anahtarlarının özeti */
        br_sha256_context c; br_sha256_init(&c);
        for (unsigned i = 0; i < TULPAR_KOK_SAYISI; i++) {
            br_sha256_update(&c, TULPAR_KOKLER[i].dn.data, TULPAR_KOKLER[i].dn.len);
        }
        uint8_u o[32]; br_sha256_out(&c, o); hex(h, o, 8);
        s[0] = 0; sayi_yaz(t, TULPAR_KOK_SAYISI, 1); dize_ekle(s, sizeof s, t);
        dize_ekle(s, sizeof s, " Mozilla kökü · ad özeti "); dize_ekle(s, sizeof s, h);
        satir(&y, "TULPAR güven listesi (HTTPS)", s, R_YAZI, 0);
    }
    muhur_y = y;
    pencere_yazi(pen, 16, y, "Dosya mührü (SHA-256):", R_SONUK);
    pencere_dikdortgen(pen, 200, y - 3, 200, 22, yaziyor ? 0xFFFFFFu : 0x0F1E25u);
    pencere_yazi(pen, 206, y, dosya, yaziyor ? 0x10181Cu : R_YAZI);
    pencere_dikdortgen(pen, 410, y - 3, 80, 22, 0x2A4652u);
    pencere_yazi(pen, 420, y, "Mühürle", R_YAZI);
    y += 26;
    pencere_yazi(pen, 30, y, muhur, muhur_renk); y += 18;
    pencere_yazi(pen, 30, y, muhur_not, R_SONUK);
    pencere_sun(pen);
}

int main(void) {
    pen = pencere_ac("TAMGA — Dijital Kimlik", 150, 80, GEN, YUK);
    if (pen < 0) return 1;
    muhur_hesapla();
    ciz();
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT) { pencere_kapat(pen); return 0; }
            if (e.tur == OLAY_TIK) {
                int icinde = e.y >= muhur_y - 3 && e.y < muhur_y + 19;
                if (icinde && e.x >= 410 && e.x < 490) { yaziyor = 0; muhur_hesapla(); }
                else if (icinde && e.x >= 200 && e.x < 400) yaziyor = 1;
                else yaziyor = 0;
            } else if (e.tur == OLAY_TUS) {
                int n = 0; while (dosya[n]) n++;
                if (!yaziyor) { if (e.tus == 27) { pencere_kapat(pen); return 0; } if (e.tus == 13) muhur_hesapla(); }
                else if (e.tus == 13) { yaziyor = 0; muhur_hesapla(); }
                else if (e.tus == 27) yaziyor = 0;
                else if (e.tus == 8 && n > 0) dosya[n - 1] = 0;
                else if (e.tus > 32 && e.tus < 127 && n < 12) {
                    char c = (char)e.tus; if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                    dosya[n] = c; dosya[n + 1] = 0;
                }
            }
            ciz();
        }
        if (!oldu) sleep_ticks(4);
    }
}
