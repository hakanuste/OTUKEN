/* us1_uret — ÖTÜKEN'deki üretimin AYNISI, ana makinede.
 * UMAY'ın us1-yerel arka ucu gibi: "S: <soru>\nU: " istemi, KV önbellekli
 * us1_adim, açgözlü seçim, satır sonunda durma. Soruları stdin'den satır
 * satır okur, her yanıtı tek satır yazar. olc.py bunu Python referansıyla
 * karşılaştırır (tamsayı motoru eğitimdeki modelle aynı metni mi söylüyor).
 *
 * Kullanım: us1_uret umay.us1 sozluk.txt < sorular.txt */
#include "../c/us1.h"
#include <stdio.h>
#include <string.h>

static uint8_t veri[4 * 1024 * 1024];
static char    sozluk[US1_AZAMI_SOZLUK + 1];
static int32_t satir[US1_AZAMI_SOZLUK];

static int im_bul(const us1_model_t *m, unsigned char c) {
    for (uint32_t i = 0; i < m->sozluk; i++) if ((unsigned char)sozluk[i] == c) return (int)i;
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "kullanim: us1_uret model sozluk\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    uint32_t boy = (uint32_t)fread(veri, 1, sizeof veri, f); fclose(f);
    us1_model_t m;
    if (us1_yukle(&m, veri, boy) != US1_OLDU) { fprintf(stderr, "model bozuk\n"); return 2; }
    f = fopen(argv[2], "rb");
    if (!f) return 2;
    size_t sn = fread(sozluk, 1, sizeof sozluk - 1, f); fclose(f);
    if (sn < m.sozluk) { fprintf(stderr, "sozluk eksik\n"); return 2; }

    char soru[512];
    static uint32_t gecmis[4096];
    while (fgets(soru, sizeof soru, stdin)) {
        size_t L = strlen(soru);
        while (L && (soru[L - 1] == '\n' || soru[L - 1] == '\r')) soru[--L] = 0;
        char istem[600];
        snprintf(istem, sizeof istem, "S: %s\nU: ", soru);
        uint32_t G = 0;
        us1_sifirla(&m);
        for (char *c = istem; *c; c++) {
            int im = im_bul(&m, (unsigned char)*c);
            if (im < 0) continue;
            gecmis[G++] = (uint32_t)im;
            if (us1_adim(&m, (uint32_t)im, satir) == US1_DOLU) break;
        }
        char cikti[400]; uint32_t y = 0;
        for (int adim = 0; adim < 160 && y + 1 < sizeof cikti; adim++) {
            uint32_t im = us1_sec(satir, m.sozluk, 0);
            if (sozluk[im] == '\n') break;
            if (sozluk[im] == ' ' && y >= 8 && cikti[y - 1] == '.') break;   /* cümle sonu */
            cikti[y++] = sozluk[im];
            gecmis[G++] = im;
            int r = us1_adim(&m, im, satir);
            if (r == US1_DOLU) {
                uint32_t k = m.baglam / 2;
                us1_sifirla(&m);
                for (uint32_t j = G - k; j < G; j++) us1_adim(&m, gecmis[j], satir);
            }
        }
        cikti[y] = 0;
        printf("%s\n", cikti);
        fflush(stdout);
    }
    return 0;
}
