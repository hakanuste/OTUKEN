/* =====================================================================
 * KUT — kimlik ve yetkilendirme (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM (salt okunur). KUT'un kullanıcılarını,
 * rollerini ve süreç-kullanıcı bağlarını gösterir. Dürüst not: masaüstü
 * uygulamaları bugün kullanıcı kimliğiyle değil, TÖRE profiliyle koşar.
 * ===================================================================== */
#include "uyg.h"

#define GEN 560
#define YUK 420
static int pen;

static void ciz(void) {
    guvenlik_u g; char t[16];
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "KUT — Kimlik ve Yetki", R_VURGU);
    int y = 44;
    pencere_yazi(pen, 16, y, "Kullanıcılar", R_VURGU); y += 22;
    pencere_yazi(pen, 16, y, "uid   ad              rol", R_SONUK); y += 18;
    int n = 0;
    for (; guvenlik_bilgi(GB_KULLANICI, n, &g) == 0 && n < 8; n++, y += 18) {
        sayi_yaz(t, g.kullanici.uid, 1); pencere_yazi(pen, 16, y, t, R_YAZI);
        pencere_yazi(pen, 64, y, g.kullanici.ad, R_YAZI);
        pencere_yazi(pen, 192, y, g.kullanici.rol_ad, g.kullanici.rol == 0 ? R_UYARI : R_YAZI);
    }
    if (n == 0) { pencere_yazi(pen, 16, y, "(tanımlı kullanıcı yok)", R_SONUK); y += 18; }
    y += 14;
    pencere_yazi(pen, 16, y, "Süreç bağları", R_VURGU); y += 22;
    pencere_yazi(pen, 16, y, "süreç  →  uid", R_SONUK); y += 18;
    int b = 0;
    for (; guvenlik_bilgi(GB_KUT_BAG, b, &g) == 0 && b < 8; b++, y += 18) {
        char s[40]; s[0] = 0;
        sayi_yaz(t, g.bag.pid, 1); dize_ekle(s, sizeof s, t); dize_ekle(s, sizeof s, "  →  ");
        sayi_yaz(t, g.bag.uid, 1); dize_ekle(s, sizeof s, t);
        pencere_yazi(pen, 16, y, s, R_YAZI);
    }
    if (b == 0) { pencere_yazi(pen, 16, y, "(bağ yok)", R_SONUK); y += 18; }
    pencere_yazi(pen, 16, YUK - 50, "Not: masaüstü uygulamaları kullanıcı kimliğiyle değil,", R_SONUK);
    pencere_yazi(pen, 16, YUK - 32, "kendi TÖRE profiliyle koşar (bkz. TÖRE penceresi).", R_SONUK);
    pencere_sun(pen);
}

int main(void) {
    pen = pencere_ac("KUT — Yetkilendirme", 260, 120, GEN, YUK);
    if (pen < 0) return 1;
    ciz();
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT || (e.tur == OLAY_TUS && e.tus == 27)) { pencere_kapat(pen); return 0; }
        }
        if (!oldu) sleep_ticks(4);
    }
}
