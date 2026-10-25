/* UMAY kanıt karar katmanı ölçüm aracı — UMAY'ın GERÇEK kodu (host portu).
 * Her soru için ajan yolunu izler: OZAN niyet/alan -> uzman (kademeli) ya
 * da BİLGE (genel) dayanak araması -> kanit_degerlendir.
 *   kanit_olc karar <belge_dizini>  < sorular   -> satır başına TSV
 *   kanit_olc siklik <belge_dizini> < sözcükler -> belge sıklığı
 * TSV: yeterli kapsama odak_kapsandi odak_terim dayanak_sayi ilk_baslik alan
 *      terimler(terim:df:kapsandi:eylem,...) kutuphane_boyu dayanak_basliklari(|) */
#include "umay.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_ozan.h"
#include "umay_bilgi.h"
#include "umay_alp.h"
#include "umay_kanit.h"
#include <stdio.h>
#include <string.h>

extern void host_yetki_ayarla(uint32_t y);

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "kullanim: kanit_olc karar|siklik belge_dizini\n"); return 2; }
    host_yetki_ayarla(0xFFFFFFFFu);
    ayar_kur(0);
    ozan_kur("Hakan");
    bilgi_doktrin_yukle();
    bilgi_tara(argv[2]);
    fprintf(stderr, "kütüphane: %d kayıt\n", bilgi_sayi());
    char satir[1024];
    while (fgets(satir, sizeof satir, stdin)) {
        size_t L = strlen(satir);
        while (L && (satir[L - 1] == '\n' || satir[L - 1] == '\r')) satir[--L] = 0;
        if (!L) continue;
        if (strcmp(argv[1], "siklik") == 0) {
            printf("%s\t%d\n", satir, kanit_belge_sikligi(satir));
            continue;
        }
        coz_t c = ozan_coz(satir);
        baglam_t d[3];
        int nereden = 0, n;
        if (c.alan != ALAN_YOK && alp_uzman(c.alan)) n = uzman_dayanak_bul(c.alan, satir, d, 3, &nereden);
        else n = kanit_dayanak_bul(satir, 0, 0, d, 3);      /* BİLGE ile aynı */
        kanit_t k;
        kanit_degerlendir(satir, d, n, 0, &k);
        char ay[512] = "";
        for (int t = 0; t < k.terim_sayi; t++) {
            char p[64];
            snprintf(p, sizeof p, "%s%s:%d:%d:%d", t ? "," : "", k.terim[t], k.df[t],
                     k.kapsandi[t], k.eylem[t]);
            strncat(ay, p, sizeof ay - strlen(ay) - 1);
        }
        char bl[400] = "";
        for (int i = 0; i < n; i++) {
            if (i) strncat(bl, "|", sizeof bl - strlen(bl) - 1);
            strncat(bl, d[i].baslik, sizeof bl - strlen(bl) - 1);
        }
        printf("%d\t%d\t%d\t%s\t%d\t%s\t%s\t%s\t%d\t%s\n", k.yeterli, k.kapsama, k.odak_kapsandi,
               k.odak >= 0 ? k.terim[k.odak] : "-", n, n ? d[0].baslik : "-", alan_adi(c.alan),
               ay[0] ? ay : "-", bilgi_sayi(), n ? bl : "-");
        fflush(stdout);
    }
    return 0;
}
