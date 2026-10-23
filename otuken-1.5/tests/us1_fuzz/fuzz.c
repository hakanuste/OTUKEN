/* US1 model ayrıştırıcısı (us1_yukle) + çıkarım: bozuk dosya fuzz sınaması.
 *
 * ASan + UBSan ile derlenir (calistir.sh). Her deneme bozulmuş dosyayı TAM
 * boyunda ayrılmış yığın tamponuna koyar (sonrası ASan kırmızı bölgesi):
 * ayrıştırıcı ya da çıkarım dosya dışını okur/yazar ya da tanımsız davranışa
 * düşerse ASan/UBSan süreci durdurur -> sınama KALDI.
 *
 *  1) Hedefli durumlar: her biri REDDEDİLMELİ (beklenen kod ile).
 *  2) Rastgele bozma (belirlenimci tohum): bit çevirme, bayt yazma, başlık
 *     alanına uç değer, tensör başlığına rastgele satır/sütun, ölçeğe
 *     NaN/Inf/negatif/dev/alt-normal, kesme, uzatma. Yüklenen her bozuk
 *     dosyada us1_ileri + us1_adim da koşulur (bellek güvenliği).
 * Kullanım: fuzz <gecerli.us1> [deneme_sayisi] */
#include "../../us1/c/us1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t g_t = 0x1234567u;
static uint32_t rnd(void) { g_t ^= g_t << 13; g_t ^= g_t >> 17; g_t ^= g_t << 5; return g_t; }

static int gecti = 0, kaldi = 0;
static void dene(const char *ad, int k) {
    printf("  [%s] %s\n", k ? "GECTI" : "KALDI", ad);
    if (k) gecti++; else kaldi++;
}

static uint8_t *kopya(const uint8_t *v, uint32_t n) {
    uint8_t *p = malloc(n ? n : 1);
    if (n) memcpy(p, v, n);
    return p;
}
static void yaz32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t oku32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* tensör başlık ofsetleri (geçerli dosyada) */
static uint32_t ofs[64]; static int ofs_n = 0;
static void tensorleri_bul(const uint8_t *v, uint32_t boy) {
    uint32_t katman = oku32(v + 12), p = 48;
    int toplam = 1 + 9 * (int)katman + 2;
    ofs_n = 0;
    for (int i = 0; i < toplam && p + 8 <= boy; i++) {
        ofs[ofs_n++] = p;
        uint32_t s = oku32(v + p), u = oku32(v + p + 4);
        p += 8 + s * (4 + u);
    }
}

static int yukle_ve_kos(const uint8_t *v, uint32_t n) {
    uint8_t *t = kopya(v, n);
    us1_model_t m;
    int r = us1_yukle(&m, t, n);
    if (r == US1_OLDU) {
        static int32_t mantik[US1_MANTIK_BOY];
        uint32_t imler[8];
        for (int i = 0; i < 8; i++) imler[i] = (uint32_t)i % m.sozluk;
        us1_ileri(&m, imler, m.baglam < 8 ? m.baglam : 8, mantik);
        us1_sifirla(&m);
        int32_t satir[US1_AZAMI_SOZLUK];
        for (uint32_t i = 0; i < 12 && i < m.baglam; i++) us1_adim(&m, i % m.sozluk, satir);
    }
    free(t);
    return r;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "kullanim: fuzz model.us1 [n]\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);
    int N = argc > 2 ? atoi(argv[2]) : 20000;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t g[4 << 20];
    uint32_t boy = (uint32_t)fread(g, 1, sizeof g, f);
    fclose(f);
    tensorleri_bul(g, boy);

    printf("1) hedefli durumlar\n");
    dene("geçerli model yüklenir ve koşar", yukle_ve_kos(g, boy) == US1_OLDU);
    struct { const char *ad; uint32_t ofs; uint32_t deger; int beklenen; } alan[] = {
        { "sihir bozuk (US2)",     0, 0x00325355u, US1_BOZUK },
        { "sürüm 2 (bilinmeyen)",  4, 2,            US1_SURUM },
        { "sürüm 0",               4, 0,            US1_SURUM },
        { "d = 0",                 8, 0,            US1_BOZUK },
        { "d > azami",             8, US1_AZAMI_D + 1, US1_BOZUK },
        { "d = 2^32-1",            8, 0xFFFFFFFFu,  US1_BOZUK },
        { "katman = 0",           12, 0,            US1_BOZUK },
        { "katman > azami",       12, US1_AZAMI_KATMAN + 1, US1_BOZUK },
        { "baş = 0",              16, 0,            US1_BOZUK },
        { "baş d'yi bölmüyor",    16, 5,            US1_BOZUK },
        { "baş > d",              16, 192,          US1_BOZUK },
        { "ffn = 0",              20, 0,            US1_BOZUK },
        { "ffn > azami",          20, US1_AZAMI_FFN + 1, US1_BOZUK },
        { "sözlük = 0",           24, 0,            US1_BOZUK },
        { "sözlük > azami",       24, US1_AZAMI_SOZLUK + 1, US1_BOZUK },
        { "bağlam = 0",           28, 0,            US1_BOZUK },
        { "bağlam > azami",       28, US1_AZAMI_BAGLAM + 1, US1_BOZUK },
        { "ayrılmış alan dolu",   32, 7,            US1_BOZUK },
    };
    for (unsigned i = 0; i < sizeof alan / sizeof alan[0]; i++) {
        uint8_t *t = kopya(g, boy); yaz32(t + alan[i].ofs, alan[i].deger);
        char ad[96]; snprintf(ad, sizeof ad, "reddedilir: %s", alan[i].ad);
        dene(ad, yukle_ve_kos(t, boy) == alan[i].beklenen);
        free(t);
    }
    /* tensör biçimi: wq satırı d+1 (eski ayrıştırıcıda durağan tampon taşması) */
    {
        /* dosyayı yeniden kurmak yerine: wk'nin satirini değiştir, boy aynı kalmaz ->
         * yalnız biçim denetimi mi, boy denetimi mi yakaladı ayırt edilemez; bu
         * yüzden sütunu değiştirip satırı telafi eden durum da denenir. */
        uint32_t o = ofs[2];     /* katman 0 wq */
        uint8_t *t = kopya(g, boy);
        uint32_t s = oku32(t + o), u = oku32(t + o + 4);
        /* aynı bayt sayısı: satir*(4+sutun) sabit kalsın diye satir 2 kat,
         * sutun (u-4)/2 ... tam bölünmezse geç */
        if ((u + 4) % 2 == 0) {
            yaz32(t + o, s * 2); yaz32(t + o + 4, (u + 4) / 2 - 4);
            dene("reddedilir: wq biçimi yanlış (bayt sayısı aynı)", yukle_ve_kos(t, boy) == US1_BOZUK);
        }
        free(t);
    }
    {
        uint8_t *t = kopya(g, boy);
        yaz32(t + ofs[0], oku32(t + ofs[0]) + 1);       /* gömme satırı +1 */
        dene("reddedilir: gömme satır sayısı sözlükle uyuşmuyor", yukle_ve_kos(t, boy) == US1_BOZUK);
        free(t);
    }
    float kotu[] = { NAN, INFINITY, -INFINITY, -0.5f, 1e30f, 1e-42f /* alt-normal */ };
    const char *kotu_ad[] = { "NaN", "+Inf", "-Inf", "negatif", "dev (1e30)", "alt-normal" };
    for (int i = 0; i < 6; i++) {
        uint8_t *t = kopya(g, boy);
        memcpy(t + ofs[3] + 8, &kotu[i], 4);                /* wk satır 0 ölçeği */
        char ad[80]; snprintf(ad, sizeof ad, "reddedilir: ölçek %s", kotu_ad[i]);
        int beklenen = (i == 5) ? US1_OLDU : US1_BOZUK;   /* alt-normal ~0: zararsız, kabul */
        if (i == 5) snprintf(ad, sizeof ad, "kabul: ölçek alt-normal (~0, zararsız)");
        dene(ad, yukle_ve_kos(t, boy) == beklenen);
        free(t);
    }
    dene("reddedilir: kesik dosya (son bayt eksik)", yukle_ve_kos(g, boy - 1) == US1_BOZUK);
    dene("reddedilir: yalnız başlık", yukle_ve_kos(g, 48) == US1_BOZUK);
    dene("reddedilir: 47 bayt", yukle_ve_kos(g, 47) == US1_BOZUK);
    {
        uint8_t *t = malloc(boy + 16); memcpy(t, g, boy); memset(t + boy, 0xAB, 16);
        dene("reddedilir: sonda fazladan bayt", yukle_ve_kos(t, boy + 16) == US1_BOZUK);
        free(t);
    }

    printf("2) rastgele bozma (%d deneme, ASan+UBSan)\n", N);
    int yuklenen = 0;
    for (int it = 0; it < N; it++) {
        uint32_t n = boy;
        uint8_t *t = malloc(boy + 64); memcpy(t, g, boy);
        int tur = (int)(rnd() % 8);
        switch (tur) {
        case 0: for (int k = 0; k < 1 + (int)(rnd() % 8); k++) t[rnd() % boy] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: for (int k = 0; k < 1 + (int)(rnd() % 4); k++) t[rnd() % boy] = (uint8_t)rnd(); break;
        case 2: { static const uint32_t uc[] = {0,1,2,3,4,95,96,97,127,128,255,256,257,65535,0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu};
                  yaz32(t + 4 + 4 * (rnd() % 11), uc[rnd() % (sizeof uc / 4)]); break; }
        case 3: { uint32_t o = ofs[rnd() % ofs_n]; yaz32(t + o + (rnd() % 2) * 4, rnd() % 600); break; }
        case 4: { uint32_t o = ofs[rnd() % ofs_n]; uint32_t sat = oku32(g + o), sut = oku32(g + o + 4);
                  uint32_t r = rnd() % sat; float v; uint32_t bb = rnd();
                  memcpy(&v, &bb, 4); memcpy(t + o + 8 + r * (4 + sut), &v, 4); break; }
        case 5: n = rnd() % boy; break;
        case 6: n = boy + 1 + rnd() % 63; for (uint32_t k = boy; k < n; k++) t[k] = (uint8_t)rnd(); break;
        case 7: { uint32_t o = ofs[rnd() % ofs_n]; yaz32(t + o, oku32(g + o) + (rnd() % 3) - 1);
                  yaz32(t + o + 4, oku32(g + o + 4) + (rnd() % 3) - 1); break; }
        }
        if (yukle_ve_kos(t, n) == US1_OLDU) yuklenen++;
        free(t);
    }
    printf("     %d denemeden %d'i yüklendi (zararsız bozmalar), çökme/ASan/UBSan yok\n", N, yuklenen);
    dene("rastgele bozmalarda bellek hatası ve tanımsız davranış yok", 1);

    printf("\n=== US1 FUZZ: %d gecti, %d kaldi ===\n", gecti, kaldi);
    return kaldi ? 1 : 0;
}
