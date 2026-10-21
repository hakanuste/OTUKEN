/* =====================================================================
 * DEDE RENDER — cekirdekteki dede.c'yi ana bilgisayarda kosturur.
 *
 * NEDEN: sesi duyulabilir ve OLCULEBILIR kilmak icin WAV gerekiyor.
 * Daha once bunun icin dede.c'nin Python kopyasi tutuluyordu; kopya
 * sapti (karisim orani farkliydi) ve olcum yanlis dosyayi olctu.
 * Burada kaynagin TA KENDISI derleniyor: olculen ses, cekirdekte
 * calisan ses.
 *
 * ses_cal cagrilarini yakalar, tek kanala indirir, WAV'a yazar.
 * Ayrica icerideki KIRPILMA sayacini bildirir: sinyal ic sinira
 * (1<<28) dayaniyorsa formant yapisi bozulur, ses konusmaz.
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --- dede.c'nin bekledigi ses katmani --- */
static int16_t  *g_kare;          /* tek kanal */
static size_t    g_boy, g_sig;
unsigned long    dede_kirpma;     /* dede.c icinden artirilir */
unsigned long    dede_tasma;
unsigned long    dede_ornek;

int ses_var(void) { return 1; }

int ses_cal(const int16_t *pcm, uint32_t kare_sayisi)
{
    if (g_boy + kare_sayisi > g_sig) {
        g_sig = (g_boy + kare_sayisi) * 2 + 65536;
        g_kare = realloc(g_kare, g_sig * sizeof(int16_t));
        if (!g_kare) { fprintf(stderr, "bellek yetmedi\n"); exit(1); }
    }
    for (uint32_t i = 0; i < kare_sayisi; i++)
        g_kare[g_boy++] = pcm[i * 2];      /* iki kanal ayni; soldan al */
    return 0;
}

void ses_dur(void) { }
void serial_write(const char *s) { fputs(s, stderr); }

/* Cekirdek kaynagini oldugu gibi derle. */
#include DEDE_KAYNAK

static void wav_yaz(const char *yol, const int16_t *k, size_t n, int hz)
{
    FILE *f = fopen(yol, "wb");
    if (!f) { perror(yol); exit(1); }
    uint32_t veri = (uint32_t)(n * 2), boy = 36 + veri;
    uint32_t oran = (uint32_t)hz * 2;
    uint16_t bir = 1, kanal = 1, bit = 16, hiza = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&boy, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t on = 16; fwrite(&on, 4, 1, f);
    fwrite(&bir, 2, 1, f); fwrite(&kanal, 2, 1, f);
    uint32_t h = (uint32_t)hz; fwrite(&h, 4, 1, f);
    fwrite(&oran, 4, 1, f); fwrite(&hiza, 2, 1, f); fwrite(&bit, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&veri, 4, 1, f);
    fwrite(k, 2, n, f);
    fclose(f);
}

#ifdef DEDE_IZ
/* Zincirin katlarini metin olarak doker: her satir bir ornek. */
static FILE *g_iz;
void dede_iz_dok(int32_t kaynak, int32_t egim, int32_t yol,
                 int32_t turev, int32_t cikis)
{
    if (!g_iz) return;
    fprintf(g_iz, "%d %d %d %d %d\n", kaynak, egim, yol, turev, cikis);
}
#endif

int main(int argc, char **argv)
{
    const char *metin = (argc > 1) ? argv[1]
                      : "Ötüken'in yurdunda ocak tütüyor.";
    const char *yol   = (argc > 2) ? argv[2] : "cik/dede.wav";

#ifdef DEDE_IZ
    g_iz = fopen(getenv("DEDE_IZ_DOSYA") ? getenv("DEDE_IZ_DOSYA") : "iz.txt", "w");
#endif
    if (dede_konus(metin) != 0) { fprintf(stderr, "dede_konus basarisiz\n"); return 1; }

#ifdef DEDE_IZ
    if (g_iz) { fclose(g_iz); g_iz = 0; }
#endif
    wav_yaz(yol, g_kare, g_boy, 48000);
    printf("%s  %.2f sn  %zu kare  kirpma=%lu tasma=%lu oran=%.5f\n",
           yol, (double)g_boy / 48000.0, g_boy, dede_kirpma, dede_tasma,
           dede_ornek ? (double)dede_tasma / (double)dede_ornek : 0.0);
    return 0;
}
