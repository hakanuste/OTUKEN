/* =====================================================================
 * ULAK — ağ durumu ve tanı (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM | NET_SEND | NET_RECV. Gösterir: kart,
 * adres (DHCP), ağ geçidi, DNS, sayaçlar, açık TCP bağlantıları. Tanı:
 * bir adı çöz ve TCP bağlantısı dene (süreyle). Saniyede bir tazelenir.
 * ===================================================================== */
#include "uyg.h"



#define GEN 600
#define YUK 470
#define ARKA   R_ARKA
#define PANO   R_PANO
#define YAZI   R_YAZI
#define SONUK  R_SONUK
#define VURGU  R_VURGU
#define UYARI  R_UYARI
#define KIRMIZI R_KIRMIZI

static int pen;
static char giris[128] = "example.com";
static int imlec = 11, yaziyor = 0;
static char sonuc[160];
static uint32_u sonuc_renk = SONUK;
static long hz;

static void ip_yaz(char *s, int boy, const uint8_u ip[4]) {
    char t[8];
    for (int i = 0; i < 4; i++) { sayi_yaz(t, ip[i], 1); dize_ekle(s, boy, t); if (i < 3) dize_ekle(s, boy, "."); }
}
static void satir(int y, const char *e, const char *d, uint32_u r) { pencere_yazi(pen, 16, y, e, SONUK); pencere_yazi(pen, 170, y, d, r); }

static const char *durum_adi(int d) {
    switch (d) { case 0: return "kapalı"; case 1: return "bağlanıyor"; case 2: return "kurulu";
    case 3: return "kapanıyor"; case 4: return "zaman aşımı"; case 5: return "karşı kapadı"; default: return "son onay"; }
}

static void ciz(void) {
    ag_bilgi_u b; char s[160], t[24];
    int bil = ag_bilgi(&b) == 0;
    pencere_doldur(pen, ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, PANO);
    pencere_yazi(pen, 14, 7, "ULAK — Ağ Durumu", VURGU);
    int y = 42;
    if (!bil) { pencere_yazi(pen, 16, y, "ağ bilgisi alınamadı", KIRMIZI); pencere_sun(pen); return; }
    s[0] = 0; dize_ekle(s, sizeof s, b.bagli ? "bağlı" : "KABLO YOK");
    satir(y, "Kart (e1000)", s, b.bagli ? VURGU : KIRMIZI); y += 20;
    s[0] = 0;
    for (int i = 0; i < 6; i++) { t[0] = "0123456789ABCDEF"[b.mac[i] >> 4]; t[1] = "0123456789ABCDEF"[b.mac[i] & 15]; t[2] = 0; dize_ekle(s, sizeof s, t); if (i < 5) dize_ekle(s, sizeof s, ":"); }
    satir(y, "MAC", s, YAZI); y += 20;
    if (b.yapilandi) {
        s[0] = 0; ip_yaz(s, sizeof s, b.ip); dize_ekle(s, sizeof s, " / "); ip_yaz(s, sizeof s, b.maske);
        satir(y, "Adres (DHCP)", s, YAZI); y += 20;
        s[0] = 0; ip_yaz(s, sizeof s, b.gecit); satir(y, "Ağ geçidi", s, YAZI); y += 20;
        s[0] = 0; ip_yaz(s, sizeof s, b.dns); satir(y, "DNS", s, YAZI); y += 20;
    } else { satir(y, "Adres (DHCP)", "bekleniyor…", UYARI); y += 60; }
    s[0] = 0;
    sayi_yaz(t, b.rx_paket, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " alındı · ");
    sayi_yaz(t, b.tx_paket, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " gönderildi · TCP ");
    sayi_yaz(t, b.rx_tcp, 1); dize_ekle(s, sizeof s, t);
    satir(y, "Paketler", s, YAZI); y += 30;

    pencere_dikdortgen(pen, 0, y - 6, GEN, 1, PANO);
    pencere_yazi(pen, 16, y, "TCP bağlantıları", VURGU);
    pencere_yazi(pen, 250, y, "durum", SONUK); pencere_yazi(pen, 380, y, "alınan", SONUK); pencere_yazi(pen, 480, y, "yeniden", SONUK);
    y += 20;
    static tcp_ozet_u tc[16];
    int n = (int)syscall(SYS_AG_BAGLANTILAR, (long)tc, 16, 0, 0);
    if (n <= 0) { pencere_yazi(pen, 16, y, "(açık bağlantı yok)", SONUK); y += 18; }
    for (int i = 0; i < n && i < 8; i++, y += 18) {
        s[0] = 0; ip_yaz(s, sizeof s, tc[i].ip); dize_ekle(s, sizeof s, ":"); sayi_yaz(t, tc[i].uzak, 1); dize_ekle(s, sizeof s, t);
        pencere_yazi(pen, 16, y, s, YAZI);
        pencere_yazi(pen, 250, y, durum_adi(tc[i].durum), tc[i].durum == 2 ? VURGU : SONUK);
        s[0] = 0; sayi_yaz(t, tc[i].alinan / 1024, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " KB");
        pencere_yazi(pen, 380, y, s, YAZI);
        sayi_yaz(t, tc[i].yeniden, 1); pencere_yazi(pen, 480, y, t, tc[i].yeniden ? UYARI : SONUK);
    }
    /* tanı kutusu */
    int ty = YUK - 70;
    pencere_dikdortgen(pen, 0, ty - 10, GEN, 1, PANO);
    pencere_yazi(pen, 16, ty + 4, "Sına:", SONUK);
    pencere_dikdortgen(pen, 64, ty, 300, 24, yaziyor ? 0xFFFFFFu : 0x0F1E25u);
    pencere_yazi(pen, 70, ty + 4, giris, yaziyor ? 0x10181Cu : YAZI);
    if (yaziyor) pencere_dikdortgen(pen, 70 + imlec * 8, ty + 3, 2, 18, 0x1A5FB4u);
    pencere_dikdortgen(pen, 374, ty, 60, 24, 0x2A4652u); pencere_yazi(pen, 388, ty + 4, "Çöz", YAZI);
    pencere_dikdortgen(pen, 444, ty, 140, 24, 0x2A4652u); pencere_yazi(pen, 452, ty + 4, "Bağlan (:80)", YAZI);
    pencere_yazi(pen, 16, ty + 36, sonuc, sonuc_renk);
    pencere_sun(pen);
}

static void coz_dene(int baglan) {
    uint8_u ip[4];
    long t0 = ticks();
    sonuc[0] = 0; dize_ekle(sonuc, sizeof sonuc, baglan ? "bağlanılıyor…" : "çözülüyor…"); sonuc_renk = SONUK; ciz();
    if (ag_coz(giris, ip) != 0) {
        sonuc[0] = 0; dize_ekle(sonuc, sizeof sonuc, "Ad çözülemedi: "); dize_ekle(sonuc, sizeof sonuc, giris);
        sonuc_renk = KIRMIZI; return;
    }
    long t1 = ticks();
    char t[16];
    sonuc[0] = 0; dize_ekle(sonuc, sizeof sonuc, giris); dize_ekle(sonuc, sizeof sonuc, " → "); ip_yaz(sonuc, sizeof sonuc, ip);
    dize_ekle(sonuc, sizeof sonuc, " ("); sayi_yaz(t, (uint64_u)((t1 - t0) * 1000 / hz), 1); dize_ekle(sonuc, sizeof sonuc, t);
    dize_ekle(sonuc, sizeof sonuc, " ms)");
    sonuc_renk = VURGU;
    if (!baglan) return;
    int h = ag_baglan(ip, 80);
    long t2 = ticks();
    if (h < 0) { dize_ekle(sonuc, sizeof sonuc, " · 80. kapıya BAĞLANILAMADI"); sonuc_renk = KIRMIZI; return; }
    dize_ekle(sonuc, sizeof sonuc, " · TCP kuruldu ("); sayi_yaz(t, (uint64_u)((t2 - t1) * 1000 / hz), 1);
    dize_ekle(sonuc, sizeof sonuc, t); dize_ekle(sonuc, sizeof sonuc, " ms)");
    ag_kapat(h);
}

static int uzun(const char *s) { int n = 0; while (s[n]) n++; return n; }

int main(void) {
    hz = (long)sistem_bil(SYSBIL_TIK_HZ); if (hz <= 0) hz = 100;
    pen = pencere_ac("ULAK — Ağ", 200, 90, GEN, YUK);
    if (pen < 0) return 1;
    dize_ekle(sonuc, sizeof sonuc, "Kutuya ad yaz: Enter çözer; \"Bağlan\" TCP dener.");
    ciz();
    long son = ticks();
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT) { pencere_kapat(pen); return 0; }
            if (e.tur == OLAY_TIK) {
                int ty = YUK - 70;
                if (e.y >= ty && e.y < ty + 24) {
                    if (e.x >= 64 && e.x < 364) { yaziyor = 1; imlec = uzun(giris); }
                    else if (e.x >= 374 && e.x < 434) { yaziyor = 0; coz_dene(0); }
                    else if (e.x >= 444 && e.x < 584) { yaziyor = 0; coz_dene(1); }
                } else yaziyor = 0;
                ciz();
            } else if (e.tur == OLAY_TUS) {
                if (e.tus == 27 && !yaziyor) { pencere_kapat(pen); return 0; }
                if (!yaziyor) { yaziyor = 1; imlec = uzun(giris); if (e.tus >= 0x21 && e.tus < 0x7F) { giris[0] = 0; imlec = 0; } }
                int n = uzun(giris);
                if (e.tus == 13) { yaziyor = 0; coz_dene(0); }
                else if (e.tus == 27) yaziyor = 0;
                else if (e.tus == 8) { if (imlec > 0) { for (int i = imlec - 1; i < n; i++) giris[i] = giris[i + 1]; imlec--; } }
                else if (e.tus == 0xE002) { if (imlec > 0) imlec--; }
                else if (e.tus == 0xE003) { if (imlec < n) imlec++; }
                else if (e.tus >= 0x21 && e.tus < 0x7F && n < 120) {
                    for (int i = n; i >= imlec; i--) giris[i + 1] = giris[i];
                    giris[imlec++] = (char)e.tus;
                }
                ciz();
            }
        }
        if (ticks() - son >= hz && !yaziyor) { son = ticks(); ciz(); }
        if (!oldu) sleep_ticks(2);
    }
}
