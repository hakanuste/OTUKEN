/* =====================================================================
 * ÜLGEN "Görünüm" — tema seçici (1.4.8)
 * Kendi adres alanında; yetki: EKRAN|KLAVYE|SISTEM|AYAR. Temaları
 * çekirdekten okur (SYS_TEMA_BIL), tıklanan ya da 1..n tuşuyla seçilen
 * temayı uygular (SYS_TEMA). Esc / kapat düğmesi: çıkar.
 * ===================================================================== */
#include "uyg.h"

#define GEN 360
#define YUK 260
#define ARKA   0x14232Bu
#define PANO   0x1B313Au
#define YAZI   0xE8E2D0u
#define SONUK  0x8FA3A8u
#define VURGU  0x4FB3A9u
#define KIRMIZI 0xD9534Fu
#define AZAMI  6
#define DUG_Y0 48
#define DUG_ARA 44
#define DUG_YUK 36

static int pen;
static char ad[AZAMI][32];
static int adet;
static char durum[64];
static uint32_u durum_renk = SONUK;

/* Görünen ad: bilinen iç adların Türkçe yazımı; bilinmeyen olduğu gibi. */
static const char *gorunen(const char *a) {
    if (!strcmp(a, "otuken")) return "Ötüken";
    if (!strcmp(a, "gece"))   return "Gece";
    if (!strcmp(a, "bozkir")) return "Bozkır";
    return a;
}

static void ciz(void) {
    char simdiki[32];
    if (tema_bil(-1, simdiki) != 0) simdiki[0] = 0;
    pencere_doldur(pen, ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 34, PANO);
    pencere_yazi(pen, 16, 9, "ÜLGEN — Görünüm", VURGU);
    for (int i = 0; i < adet; i++) {
        int y = DUG_Y0 + i * DUG_ARA;
        int etkin = !strcmp(ad[i], simdiki);
        if (etkin) pencere_dikdortgen(pen, 14, y - 2, GEN - 28, DUG_YUK + 4, VURGU);
        pencere_dikdortgen(pen, 16, y, GEN - 32, DUG_YUK, PANO);
        char s[48];
        s[0] = (char)('1' + i); s[1] = ' '; s[2] = ' '; s[3] = 0;
        dize_ekle(s, sizeof s, gorunen(ad[i]));
        pencere_yazi(pen, 28, y + 10, s, YAZI);
        if (etkin) pencere_yazi(pen, GEN - 100, y + 10, "etkin", VURGU);
    }
    if (adet == 0) pencere_yazi(pen, 16, DUG_Y0, "tema listesi okunamadı", KIRMIZI);
    pencere_yazi(pen, 16, YUK - 44, durum, durum_renk);
    pencere_yazi(pen, 16, YUK - 22, "tıkla ya da 1..n   Esc: kapat", SONUK);
    pencere_sun(pen);
}

static void sec(int i) {
    if (i < 0 || i >= adet) return;
    durum[0] = 0;
    if (tema_degistir(ad[i]) == 0) {
        dize_ekle(durum, sizeof durum, "Uygulandı: ");
        durum_renk = VURGU;
    } else {
        dize_ekle(durum, sizeof durum, "Uygulanamadı: ");
        durum_renk = KIRMIZI;
    }
    dize_ekle(durum, sizeof durum, gorunen(ad[i]));
    ciz();
}

int main(void) {
    for (adet = 0; adet < AZAMI; adet++)
        if (tema_bil(adet, ad[adet]) != 0) break;
    pen = pencere_ac("Görünüm — ÜLGEN", 300, 120, GEN, YUK);
    if (pen < 0) return 1;
    durum[0] = 0;
    ciz();
    for (;;) {
        olay_u o;
        while (pencere_olay(pen, &o) == 1) {
            if (o.tur == OLAY_KAPAT) goto cik;
            if (o.tur == OLAY_TUS) {
                if (o.tus == 27) goto cik;
                if (o.tus >= '1' && o.tus < (uint32_u)('1' + adet)) sec((int)(o.tus - '1'));
            } else if (o.tur == OLAY_TIK) {
                for (int i = 0; i < adet; i++) {
                    int y = DUG_Y0 + i * DUG_ARA;
                    if (o.x >= 16 && o.x < GEN - 16 && o.y >= y && o.y < y + DUG_YUK) sec(i);
                }
            }
        }
        sleep_ticks(2);
    }
cik:
    pencere_kapat(pen);
    return 0;
}
