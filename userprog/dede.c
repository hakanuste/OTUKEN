/* =====================================================================
 * DEDE KORKUT — söz ve ses (1.4.9)
 * Yetki: EKRAN | KLAVYE | SISTEM | SES. Seçilen sözü (ya da yazılanı)
 * çekirdeğin Türkçe formant sentezleyicisiyle (dede.c) seslendirir.
 * Ses kartı (AC'97) yoksa bunu açıkça söyler.
 * ===================================================================== */
#include "uyg.h"

#define GEN 600
#define YUK 440
static int pen;
static int ses_var;
static int secili = -1;
static char yazi[200];
static int imlec, yaziyor;
static char durum[160];
static uint32_u durum_renk = R_SONUK;

/* Atasözleri: halkın ortak malı sözler */
static const char *const SOZ[] = {
    "Damlaya damlaya göl olur.",
    "Birlikten kuvvet doğar.",
    "Yürüyen at yol alır.",
    "Sakla samanı, gelir zamanı.",
    "Ağaç yaşken eğilir.",
    "Emek olmadan yemek olmaz.",
    "Bin işit, bir söyle.",
    "Atın ölümü arpadan olsun.",
};
#define SOZ_SAYI ((int)(sizeof SOZ / sizeof SOZ[0]))
#define LY 70
#define LA 30

static int uzun(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int kodsay(const char *s) { int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n; }

static void ciz(void) {
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "DEDE KORKUT — Söz", R_VURGU);
    pencere_yazi(pen, 14, 40, ses_var == 2 ? "Ses kartı: var (HDA)" : ses_var ? "Ses kartı: var (AC'97)" : "Ses kartı: YOK — seslendirilemez", ses_var ? R_VURGU : R_UYARI);
    for (int i = 0; i < SOZ_SAYI; i++) {
        int y = LY + i * LA;
        if (i == secili) pencere_dikdortgen(pen, 8, y - 4, GEN - 16, LA - 4, 0x24424Cu);
        pencere_yazi(pen, 20, y, SOZ[i], R_YAZI);
    }
    int ty = YUK - 80;
    pencere_yazi(pen, 14, ty - 22, "Kendi sözün:", R_SONUK);
    pencere_dikdortgen(pen, 14, ty, GEN - 120, 24, yaziyor ? 0xFFFFFFu : 0x0F1E25u);
    pencere_yazi(pen, 20, ty + 4, yazi, yaziyor ? 0x10181Cu : R_YAZI);
    if (yaziyor) {
        pencere_dikdortgen(pen, 20 + imlec * 8, ty + 3, 2, 18, 0x1A5FB4u);
    }
    pencere_dikdortgen(pen, GEN - 96, ty, 82, 24, 0x2A4652u);
    pencere_yazi(pen, GEN - 84, ty + 4, "Söyle", R_YAZI);
    pencere_yazi(pen, 14, ty + 40, durum, durum_renk);
    pencere_sun(pen);
}

static int bekliyor;      /* söz sıraya alındı, bitmesi bekleniyor */

static void durum_yaz(const char *m, uint32_u r) { durum[0] = 0; dize_ekle(durum, sizeof durum, m); durum_renk = r; ciz(); }

/* Çekirdek sözü SIRAYA alır ve hemen döner (1.4.9); bitişi SYSBIL_DEDE_MESGUL
 * ile izlenir, pencere bu arada tepki vermeye devam eder. */
static void soyle(const char *m) {
    if (!ses_var) { durum_yaz("Ses kartı yok: söylenemedi.", R_UYARI); return; }
    long r = syscall(SYS_DEDE_KONUS, (long)m, 0, 0, 0);
    if (r != 0) { durum_yaz("Seslendirme başarısız.", R_KIRMIZI); return; }
    bekliyor = 1;
    durum_yaz("Söyleniyor…", R_VURGU);
}

int main(void) {
    pen = pencere_ac("DEDE KORKUT — Söz", 220, 100, GEN, YUK);
    if (pen < 0) return 1;
    ses_var = (int)sistem_bil(SYSBIL_SES_VAR);
    dize_ekle(durum, sizeof durum, "Bir söze tıkla ya da kendi sözünü yaz, Enter.");
    ciz();
    for (;;) {
        olay_u e; int oldu = 0;
        while (pencere_olay(pen, &e) == 1) {
            oldu = 1;
            if (e.tur == OLAY_KAPAT) { pencere_kapat(pen); return 0; }
            if (e.tur == OLAY_TIK) {
                int ty = YUK - 80;
                if (e.y >= LY - 4 && e.y < LY + SOZ_SAYI * LA - 4) {
                    secili = (e.y - LY + 4) / LA; yaziyor = 0; soyle(SOZ[secili]);
                } else if (e.y >= ty && e.y < ty + 24) {
                    if (e.x >= GEN - 96) { yaziyor = 0; if (yazi[0]) soyle(yazi); }
                    else { yaziyor = 1; imlec = kodsay(yazi); ciz(); }
                } else { yaziyor = 0; ciz(); }
            } else if (e.tur == OLAY_TUS) {
                if (!yaziyor) {
                    if (e.tus == 27) { pencere_kapat(pen); return 0; }
                    if (e.tus == 0xE000 && secili > 0) { secili--; ciz(); continue; }
                    if (e.tus == 0xE001 && secili < SOZ_SAYI - 1) { secili++; ciz(); continue; }
                    if (e.tus == 13 && secili >= 0) { soyle(SOZ[secili]); continue; }
                    if (e.tus < 0x20 || (e.tus >= 0xE000 && e.tus < 0xF000)) continue;
                    yaziyor = 1; imlec = kodsay(yazi);
                }
                int n = uzun(yazi);
                int bayt = 0, k = 0; while (yazi[bayt] && k < imlec) { bayt++; while ((yazi[bayt] & 0xC0) == 0x80) bayt++; k++; }
                if (e.tus == 13) { yaziyor = 0; if (yazi[0]) soyle(yazi); continue; }
                if (e.tus == 27) { yaziyor = 0; }
                else if (e.tus == 8 && imlec > 0) {
                    int o = bayt - 1; while (o > 0 && (yazi[o] & 0xC0) == 0x80) o--;
                    for (int i = o; i <= n - (bayt - o); i++) yazi[i] = yazi[i + (bayt - o)];
                    imlec--;
                } else if (e.tus >= 0x20 && !(e.tus >= 0xE000 && e.tus < 0xF000) && n < 190) {
                    char u[4]; int ub;
                    uint32_u c = e.tus;
                    if (c < 0x80) { u[0] = (char)c; ub = 1; }
                    else if (c < 0x800) { u[0] = (char)(0xC0 | (c >> 6)); u[1] = (char)(0x80 | (c & 63)); ub = 2; }
                    else { u[0] = (char)(0xE0 | (c >> 12)); u[1] = (char)(0x80 | ((c >> 6) & 63)); u[2] = (char)(0x80 | (c & 63)); ub = 3; }
                    for (int i = n; i >= bayt; i--) yazi[i + ub] = yazi[i];
                    for (int i = 0; i < ub; i++) yazi[bayt + i] = u[i];
                    imlec++;
                }
                ciz();
            }
        }
        if (bekliyor && sistem_bil(SYSBIL_DEDE_MESGUL) == 0) { bekliyor = 0; durum_yaz("Söylendi.", R_VURGU); }
        if (!oldu) sleep_ticks(2);
    }
}
