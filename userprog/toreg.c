/* =====================================================================
 * TÖRE — güvenlik politikaları görüntüleyicisi (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM (salt okunur). İki görünüm:
 *   Görevler: her görevin TÖRE yetkileri, Ring3 mü
 *   Çağrılar: her sistem çağrısının gerektirdiği yetki
 * ===================================================================== */
#include "uyg.h"

#define GEN 760
#define YUK 500
#define UST 70
#define SAT 18
#define GOR ((YUK - UST - 40) / SAT)
static int pen, gorunum, ust;
static char yetki_ad[KUT_YETKI_SAYISI][24];

static void yetkiler(uint32_u y, char *s, int boy) {
    s[0] = 0;
    if (y == 0) { dize_ekle(s, boy, "-"); return; }
    int ilk = 1;
    for (int b = 0; b < KUT_YETKI_SAYISI; b++) if (y & (1u << b)) {
        if (!ilk) dize_ekle(s, boy, ",");
        dize_ekle(s, boy, yetki_ad[b][0] ? yetki_ad[b] : "?");
        ilk = 0;
    }
}

static int satir_say(void) {
    guvenlik_u g; int n = 0;
    while (guvenlik_bilgi(gorunum == 0 ? GB_GOREV : GB_POLITIKA, n, &g) == 0 && n < 400) n++;
    return n;
}

static void ciz(void) {
    char s[200], t[16];
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "TÖRE — Yetkiler ve Politikalar", R_VURGU);
    pencere_dikdortgen(pen, 14, 36, 100, 24, gorunum == 0 ? 0x2A4652u : 0x1B2A30u);
    pencere_yazi(pen, 26, 40, "Görevler", gorunum == 0 ? R_YAZI : R_SONUK);
    pencere_dikdortgen(pen, 124, 36, 100, 24, gorunum == 1 ? 0x2A4652u : 0x1B2A30u);
    pencere_yazi(pen, 136, 40, "Çağrılar", gorunum == 1 ? R_YAZI : R_SONUK);
    int n = satir_say();
    if (ust > n - GOR) ust = n - GOR;
    if (ust < 0) ust = 0;
    int y = UST;
    if (gorunum == 0) {
        pencere_yazi(pen, 14, y, "kimlik  ad              Ring3  yetkiler", R_SONUK); y += SAT;
    } else {
        pencere_yazi(pen, 14, y, "no  çağrı                          gereken yetki", R_SONUK); y += SAT;
    }
    for (int i = ust; i < n && y < YUK - 40; i++, y += SAT) {
        guvenlik_u g;
        if (guvenlik_bilgi(gorunum == 0 ? GB_GOREV : GB_POLITIKA, i, &g) != 0) break;
        if (gorunum == 0) {
            sayi_yaz(t, g.gorev.id, 1); pencere_yazi(pen, 14, y, t, R_YAZI);
            pencere_yazi(pen, 78, y, g.gorev.ad, R_YAZI);
            pencere_yazi(pen, 206, y, g.gorev.kullanici ? "evet" : "-", g.gorev.kullanici ? R_VURGU : R_SONUK);
            if (g.gorev.kayitli) yetkiler(g.gorev.yetki, s, sizeof s);
            else { s[0] = 0; dize_ekle(s, sizeof s, "(çekirdek: tam yetki)"); }
            pencere_yazi(pen, 262, y, s, g.gorev.kayitli ? R_YAZI : R_SONUK);
        } else {
            sayi_yaz(t, (uint64_u)g.politika.no, 1); pencere_yazi(pen, 14, y, t, R_SONUK);
            pencere_yazi(pen, 46, y, g.politika.ad, R_YAZI);
            if (g.politika.yetki < 0) { s[0] = 0; dize_ekle(s, sizeof s, "YOK (reddedilir)"); }
            else if (g.politika.yetki == 0) { s[0] = 0; dize_ekle(s, sizeof s, "serbest"); }
            else yetkiler((uint32_u)g.politika.yetki, s, sizeof s);
            pencere_yazi(pen, 310, y, s, g.politika.yetki < 0 ? R_UYARI : g.politika.yetki == 0 ? R_SONUK : R_VURGU);
        }
    }
    guvenlik_u tm;
    s[0] = 0;
    if (guvenlik_bilgi(GB_TIMUR, 0, &tm) == 0) {
        dize_ekle(s, sizeof s, "Reddedilen çağrı: "); sayi_yaz(t, tm.timur.reddedilen, 1); dize_ekle(s, sizeof s, t);
        dize_ekle(s, sizeof s, " · politikası yazılmamış çağrı: "); sayi_yaz(t, tm.timur.politikasiz, 1); dize_ekle(s, sizeof s, t);
    }
    pencere_yazi(pen, 14, YUK - 26, s, R_SONUK);
    pencere_yazi(pen, GEN - 200, YUK - 26, "↑↓ PgUp PgDn kaydır", R_SONUK);
    pencere_sun(pen);
}

int main(void) {
    for (int b = 0; b < KUT_YETKI_SAYISI; b++) {
        guvenlik_u g;
        if (guvenlik_bilgi(GB_YETKI_ADI, b, &g) == 0) { int i = 0; while (g.yetki_adi.ad[i] && i < 23) { yetki_ad[b][i] = g.yetki_adi.ad[i]; i++; } yetki_ad[b][i] = 0; }
    }
    pen = pencere_ac("TÖRE — Güvenlik Politikaları", 120, 80, GEN, YUK);
    if (pen < 0) return 1;
    ciz();
    long son = ticks(), hz = (long)sistem_bil(SYSBIL_TIK_HZ); if (hz <= 0) hz = 100;
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT || (e.tur == OLAY_TUS && e.tus == 27)) { pencere_kapat(pen); return 0; }
            if (e.tur == OLAY_TIK && e.y >= 36 && e.y < 60) {
                if (e.x >= 14 && e.x < 114) { gorunum = 0; ust = 0; }
                else if (e.x >= 124 && e.x < 224) { gorunum = 1; ust = 0; }
            } else if (e.tur == OLAY_TUS) {
                if (e.tus == 0xE000) ust--;
                else if (e.tus == 0xE001) ust++;
                else if (e.tus == 0xE004) ust -= GOR - 2;
                else if (e.tus == 0xE005) ust += GOR - 2;
                else if (e.tus == '\t' || e.tus == 0xE002 || e.tus == 0xE003) { gorunum = !gorunum; ust = 0; }
            }
            ciz();
        }
        if (ticks() - son >= 2 * hz) { son = ticks(); ciz(); }
        if (!oldu) sleep_ticks(2);
    }
}
