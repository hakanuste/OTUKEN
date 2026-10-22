/* =====================================================================
 * TİMUR — güvenlik korumalarının durumu (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM (salt okunur). Açık olanı da, OLMAYANI
 * da gösterir: kapalı koruma "kapalı" diye yazar, süslenmez.
 * ===================================================================== */
#include "uyg.h"

#define GEN 640
#define YUK 470
static int pen;

static void madde(int *y, const char *ad, const char *durum, uint32_u renk, const char *not) {
    pencere_yazi(pen, 16, *y, ad, R_YAZI);
    pencere_yazi(pen, 250, *y, durum, renk);
    if (not && *not) pencere_yazi(pen, 34, *y + 16, not, R_SONUK);
    *y += (not && *not) ? 40 : 24;
}

static void ciz(void) {
    guvenlik_u g; char s[120], t[16];
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "TİMUR — Korumalar", R_VURGU);
    int y = 44;
    if (guvenlik_bilgi(GB_TIMUR, 0, &g) != 0) { pencere_yazi(pen, 16, y, "durum okunamadı", R_KIRMIZI); pencere_sun(pen); return; }
    madde(&y, "NX (veri çalıştırılamaz)", g.timur.nx_acik ? "açık" : (g.timur.nx_destek ? "KAPALI" : "işlemcide yok"),
          g.timur.nx_acik ? R_VURGU : R_KIRMIZI, "yığın ve veri sayfaları çalıştırılamaz");
    {   /* W^X: veri sayfası NX (EFER.NXE) + kod sayfası salt okunur, çekirdek de uyar (CR0.WP) */
        int wx = g.timur.nx_acik && g.timur.wp;
        madde(&y, "W^X (yaz ya da çalıştır)", wx ? "uygulanıyor" : (g.timur.wp ? "EKSİK (NX yok)" : "EKSİK (CR0.WP kapalı)"),
              wx ? R_VURGU : R_KIRMIZI, "ELF yükleyici kod ve veri segmentlerini ayırır");
    }
    s[0] = 0; sayi_yaz(t, g.timur.ring3_gorev, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, " Ring3 görevi");
    madde(&y, "Ayrı adres alanları", s, R_VURGU, "masaüstü uygulamaları kendi adres alanında");
    madde(&y, "SMEP", g.timur.smep_acik ? "açık" : "kapalı", g.timur.smep_acik ? R_VURGU : R_UYARI,
          g.timur.smep_destek ? "işlemci destekliyor; çekirdek henüz açmıyor" : "işlemcide yok");
    madde(&y, "SMAP", g.timur.smap_acik ? "açık" : "kapalı", g.timur.smap_acik ? R_VURGU : R_UYARI,
          g.timur.smap_destek ? "işlemci destekliyor; çekirdek henüz açmıyor" : "işlemcide yok");
    s[0] = 0; dize_ekle(s, sizeof s, "reddedilen "); sayi_yaz(t, g.timur.reddedilen, 1); dize_ekle(s, sizeof s, t);
    dize_ekle(s, sizeof s, " · politikasız "); sayi_yaz(t, g.timur.politikasiz, 1); dize_ekle(s, sizeof s, t);
    madde(&y, "TÖRE yetki denetimi", s, g.timur.politikasiz ? R_KIRMIZI : R_VURGU, "her çağrı yetki ister; tanımsız çağrı reddedilir");
    s[0] = 0; dize_ekle(s, sizeof s, "imza zorunlu · kurulu "); sayi_yaz(t, g.timur.kervan_kurulu, 1); dize_ekle(s, sizeof s, t);
    madde(&y, "KERVAN paketleri", s, R_VURGU, "ECDSA P-256 + SHA-256, çekirdekte doğrulanır");
    madde(&y, "HTTPS (TULPAR)", "TLS 1.2 · 146 Mozilla kökü", R_VURGU, "doğrulanamayan site açılmaz");
    madde(&y, "Donanım rastgeleliği", g.timur.rng == 2 ? "RDSEED + RDRAND" : g.timur.rng == 1 ? "RDRAND" : "YOK (HTTPS kapalı)",
          g.timur.rng ? R_VURGU : R_KIRMIZI, 0);
    madde(&y, "Gerçek saat (RTC)", g.timur.rtc ? "var" : "YOK", g.timur.rtc ? R_VURGU : R_UYARI, 0);
    pencere_sun(pen);
}

int main(void) {
    pen = pencere_ac("TİMUR — Güvenlik", 180, 70, GEN, YUK);
    if (pen < 0) return 1;
    ciz();
    long son = ticks(), hz = (long)sistem_bil(SYSBIL_TIK_HZ); if (hz <= 0) hz = 100;
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT || (e.tur == OLAY_TUS && e.tus == 27)) { pencere_kapat(pen); return 0; }
        }
        if (ticks() - son >= 2 * hz) { son = ticks(); ciz(); }
        if (!oldu) sleep_ticks(2);
    }
}
