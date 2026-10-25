/* US1 sözünün dayanağa bağlılığı — ölçüm aracı (UMAY'ın GERÇEK kodu).
 * Girdi (stdin): soru<TAB>beklenen_kayit_basligi
 * Her soru için: kanıt kapısı (kanit_degerlendir) -> YETERLİ ise gerçek US1
 * modeli (us_yerel_motor, UMAY_KOK altındaki /UMAY.US1 + /SOZLUK.TXT) ile
 * HAM söz üretilir (bağlılık denetiminden ÖNCE) ve kanit_bagliligi ölçülür.
 * Çıktı TSV: yeterli baglilik soz dayanak_basliklari(|) beklenen */
#include "umay.h"
#include "umay_str.h"
#include "umay_ortak.h"
#include "umay_ozan.h"
#include "umay_bilgi.h"
#include "umay_alp.h"
#include "umay_kanit.h"
#include "umay_us.h"
#include <stdio.h>
#include <string.h>

extern void host_yetki_ayarla(uint32_t y);

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "kullanim: baglilik_olc belge_dizini [tam] (UMAY_KOK ayarlı)\n"); return 2; }
    int tam = argc > 2 && strcmp(argv[2], "tam") == 0;   /* uçtan uca: us_cevap_uret çıktısı */
    host_yetki_ayarla(0xFFFFFFFFu);
    ayar_kur(0);
    ozan_kur("Hakan");
    bilgi_doktrin_yukle();
    bilgi_tara(argv[1]);
    umay_us_t *m = us_yerel_motor();
    if (m->hazirla(m, 0) != UM_OLDU) { fprintf(stderr, "model yüklenemedi\n"); return 1; }
    if (tam) us_motor_tak(m);
    char satir[1024];
    static char soz[US_METIN_MAX];
    while (fgets(satir, sizeof satir, stdin)) {
        size_t L = strlen(satir);
        while (L && (satir[L - 1] == '\n' || satir[L - 1] == '\r')) satir[--L] = 0;
        char *sek = strchr(satir, '\t');
        const char *beklenen = "-";
        if (sek) { *sek = 0; beklenen = sek + 1; }
        if (!satir[0]) continue;
        coz_t c = ozan_coz(satir);
        baglam_t d[3];
        int nereden = 0, n;
        if (c.alan != ALAN_YOK && alp_uzman(c.alan)) n = uzman_dayanak_bul(c.alan, satir, d, 3, &nereden);
        else n = kanit_dayanak_bul(satir, 0, 0, d, 3);
        if (tam) {
            us_derle_t di;
            memset(&di, 0, sizeof di);
            di.kisilik = ozan_kimlik(); di.soru = satir; di.niyet = NIYET_SORU;
            di.baglam = d; di.baglam_sayi = n; di.nereden = (us_nereden_t)nereden;
            di.uzman_ad = alan_adi(c.alan); di.uzman_is = "sınama";
            static us_cikti_t o;
            static char yaz[4096];
            us_cevap_uret(&di, &o);
            us_cikti_yaz(&o, yaz, sizeof yaz);
            printf("SORU: %s\n%s(kanıt: kapsama %%%d, odak %s; bağlılık %d; özütlendi %d)\n\n", satir, yaz,
                   o.kanit.kapsama, o.kanit.odak >= 0 ? o.kanit.terim[o.kanit.odak] : "-",
                   o.baglilik, o.ozutlendi);
            continue;
        }
        kanit_t k;
        kanit_degerlendir(satir, d, n, 0, &k);
        soz[0] = 0;
        int bag = -1;
        if (k.yeterli) {
            us_istem_t i = { .kisilik = "", .baglam = "", .soru = satir, .niyet = NIYET_SORU,
                             .en_cok_im = 256 };
            if (m->uret(m, &i, soz, sizeof soz) == UM_OLDU)
                bag = kanit_bagliligi(soz, d, n, 0);
            for (char *p = soz; *p; p++) if (*p == '\t' || *p == '\n') *p = ' ';
        }
        char bl[400] = "";
        for (int j = 0; j < n; j++) {
            if (j) strncat(bl, "|", sizeof bl - strlen(bl) - 1);
            strncat(bl, d[j].baslik, sizeof bl - strlen(bl) - 1);
        }
        /* sözün içerik sözcüklerinden kütüphanenin HİÇBİR kaydında geçmeyenler
         * (bozuk üretim işareti adayı) */
        int yok = 0, top = 0;
        if (soz[0]) {
            char t[32][24];
            int tn = kanit_terimleri(soz, t, 32);
            for (int j = 0; j < tn; j++) {
                uint32_t h = 0, b; const char *q = t[j];
                while (*q) { um_utf8_oku(q, &b); q += b; h++; }
                if (h < 4) continue;
                top++;
                if (kanit_belge_sikligi(t[j]) == 0) yok++;
            }
        }
        printf("%d\t%d\t%s\t%s\t%s\t%d\t%d\n", k.yeterli, bag, soz[0] ? soz : "-", n ? bl : "-",
               beklenen, top, yok);
    }
    return 0;
}
