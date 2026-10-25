/* US1 motorunun REFERANSLA sayisal karsilastirmasi. Beklenen degerler
 * arac/referans.py tarafindan AYNI model dosyasindan uretilir; birebir
 * esitlik beklenmez (Q8 niceleme + sabit nokta payi). */
#include "../c/us1.h"
#include <stdio.h>

static int gecti = 0, kaldi = 0;
static void dene(const char *ad, int k) {
    printf("  [%s] %s\n", k ? "GECTI" : "KALDI", ad);
    if (k) gecti++; else kaldi++;
}

int main(int argc, char **argv) {
    const char *yol = (argc > 1) ? argv[1] : "umay.us1"   /* us1/ dizininden kosulur (kur.sh --sina) */;
    FILE *f = fopen(yol, "rb");
    if (!f) { printf("model acilamadi: %s\n", yol); return 1; }
    static uint8_t veri[4 * 1024 * 1024];
    uint32_t boy = (uint32_t)fread(veri, 1, sizeof veri, f);
    fclose(f);

    us1_model_t m;
    dene("model yuklendi", us1_yukle(&m, veri, boy) == US1_OLDU);
    dene("bicim alanlari tutarli",
         m.d > 0 && m.katman > 0 && m.bas > 0 && m.d % m.bas == 0 &&
         m.sozluk > 0 && m.baglam > 0);

    uint8_t bozuk[64];
    for (int i = 0; i < 64; i++) bozuk[i] = (uint8_t)i;
    us1_model_t m2;
    dene("bozuk dosya reddedildi", us1_yukle(&m2, bozuk, 64) != US1_OLDU);
    veri[4] = 9;
    dene("bilinmeyen surum reddedildi", us1_yukle(&m2, veri, boy) == US1_SURUM);
    veri[4] = 1;
    dene("kisa dosya reddedildi", us1_yukle(&m2, veri, 200) == US1_BOZUK);

    uint32_t imler[US1_AZAMI_BAGLAM];
    int n = 0;
    FILE *g = fopen("sinama/referans_imler.txt", "r");
    if (g) { while (n < (int)m.baglam && fscanf(g, "%u", &imler[n]) == 1) n++; fclose(g); }
    if (n == 0) { imler[0] = 1; imler[1] = 2; n = 2; }

    static int32_t mantik[US1_MANTIK_BOY];
    dene("ileri gecis kostu",
         us1_ileri(&m, imler, (uint32_t)n, mantik) == US1_OLDU);

    uint32_t kotu[2] = { 0, 9999 };
    dene("sozluk disi im reddedildi", us1_ileri(&m, kotu, 2, mantik) == US1_BOZUK);
    us1_ileri(&m, imler, (uint32_t)n, mantik);

    FILE *r = fopen("sinama/referans_mantik.txt", "r");
    if (r) {
        double enb = 0.0, genlik = 0.0;
        int say = 0;
        for (int t = 0; t < n; t++) {
            for (uint32_t i = 0; i < m.sozluk; i++) {
                double bekle;
                if (fscanf(r, "%lf", &bekle) != 1) break;
                double bul = (double)mantik[t * m.sozluk + i] / 65536.0;
                double fark = bul - bekle;
                if (fark < 0) fark = -fark;
                double b = (bekle < 0) ? -bekle : bekle;
                if (b > genlik) genlik = b;
                if (fark > enb) enb = fark;
                say++;
            }
        }
        fclose(r);
        printf("  referans: %d deger, en buyuk fark %.4f, genlik %.4f\n",
               say, enb, genlik);
        dene("referansa yakin (Q8 + sabit nokta payi)",
             say > 0 && enb < genlik * 0.25 + 0.5);
    }

    /* Uretimde onemli olan: her konumda EN OLASI im ayni mi? */
    FILE *r2 = fopen("sinama/referans_mantik.txt", "r");
    if (r2) {
        int ayni = 0, top = 0;
        for (int t = 0; t < n; t++) {
            double enb_d = -1e30; uint32_t enb_i = 0; int tamam = 1;
            for (uint32_t i = 0; i < m.sozluk; i++) {
                double bekle;
                if (fscanf(r2, "%lf", &bekle) != 1) { tamam = 0; break; }
                if (bekle > enb_d) { enb_d = bekle; enb_i = i; }
            }
            if (!tamam) break;
            uint32_t c_i = us1_sec(&mantik[t * m.sozluk], m.sozluk, 0);
            ayni += (c_i == enb_i); top++;
        }
        fclose(r2);
        printf("  en olasi im uyusmasi: %d/%d konum\n", ayni, top);
        dene("en olasi im referansla %90+ ayni", top > 0 && ayni * 10 >= top * 9);
    }

    /* KV onbellegi: artimli gecis tam gecisle BIT BIT ayni olmali */
    {
        static uint32_t uzun[US1_AZAMI_BAGLAM];
        static int32_t tam[US1_MANTIK_BOY];
        static int32_t adim_m[US1_AZAMI_SOZLUK];
        uint32_t T = m.baglam;
        for (uint32_t t = 0; t < T; t++) uzun[t] = (t * 7u + 3u) % m.sozluk;
        us1_ileri(&m, uzun, T, tam);
        us1_sifirla(&m);
        int esit = 1;
        for (uint32_t t = 0; t < T && esit; t++) {
            if (us1_adim(&m, uzun[t], adim_m) != US1_OLDU) { esit = 0; break; }
            for (uint32_t i = 0; i < m.sozluk; i++)
                if (adim_m[i] != tam[t * m.sozluk + i]) { esit = 0; break; }
        }
        dene("KV onbellekli adim tam gecisle bit bit ayni (tum baglam)", esit);
        dene("baglam dolunca US1_DOLU", us1_adim(&m, 0, adim_m) == US1_DOLU);
        us1_sifirla(&m);
        dene("adim: sozluk disi im reddedildi", us1_adim(&m, 9999, adim_m) == US1_BOZUK);
        dene("sifirlama konumu 0 yapti", us1_konum() == 0);
    }

    uint32_t en = us1_sec(mantik, m.sozluk, 0);
    dene("belirlenimci secim gecerli im veriyor", en < m.sozluk);
    uint32_t tohum = 42;
    uint32_t s1 = us1_sec(mantik, m.sozluk, &tohum);
    tohum = 42;
    uint32_t s2 = us1_sec(mantik, m.sozluk, &tohum);
    dene("ayni tohum ayni imi veriyor", s1 == s2 && s1 < m.sozluk);

    printf("\n=== US1 SINAMA: %d gecti, %d kaldi ===\n", gecti, kaldi);
    return kaldi == 0 ? 0 : 1;
}
