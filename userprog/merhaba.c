/* =====================================================================
 * MERHABA — KERVAN paket uygulaması örneği (1.4.9, sınama paketi)
 * Diskle gelmez: imzalı bir .KRV paketinin içinde ağdan indirilir; menüde
 * "Kurulu paketler" altında görünür ve en dar paket profiliyle koşar
 * (EKRAN | KLAVYE | SISTEM: disk ve ağ yok).
 * ===================================================================== */
#include "uyg.h"

#define GEN 420
#define YUK 160

int main(void) {
    int pen = pencere_ac("Merhaba — KERVAN paketi", 300, 200, GEN, YUK);
    if (pen < 0) return 1;
    pencere_doldur(pen, R_ARKA);
    pencere_dikdortgen(pen, 0, 0, GEN, 30, R_PANO);
    pencere_yazi(pen, 14, 7, "Merhaba — KERVAN paketi", R_VURGU);
    pencere_yazi(pen, 16, 50, "Bu uygulama ağdan imzalı paketle kuruldu.", R_YAZI);
    pencere_yazi(pen, 16, 74, "Diske ve ağa erişimi yok.", R_SONUK);
    pencere_sun(pen);
    for (;;) {
        olay_u e;
        while (pencere_olay(pen, &e) == 1)
            if (e.tur == OLAY_KAPAT || (e.tur == OLAY_TUS && e.tus == 27)) { pencere_kapat(pen); return 0; }
        sleep_ticks(4);
    }
}
