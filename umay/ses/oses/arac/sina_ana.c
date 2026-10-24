/* Ana makine sınama sürücüsü: OSES modelini çalıştırır.
 *   sina_ana model.oses ids.bin [gurultu.bin] [dok.bin] > ses.f32
 * ids.bin: int64 dizi. gurultu.bin: (u32 tensor_id, u32 n, f32[n], u8 nd, i64 dims[nd]) kayıtları.
 * dok.bin: her düğüm çıktısı için özet (+ küçükse tam veri). */
#include "oses.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void *ayir(size_t n) { return malloc(n); }
static void birak(void *p) { free(p); }
static FILE *dok;
typedef struct { int id; long n; float *v; int nd; int64_t d[6]; } gur_t;
static gur_t gur[8]; static int gsay;
static int gurultu(oses_t *m, int t, oses_tensor_t *o, void *b) {
    (void)b;
    for (int i = 0; i < gsay; i++) if (gur[i].id == t) {
        oses_tensor_t s = {0};
        oses_tampon_t *tb = malloc(sizeof *tb); tb->sayac = 1; tb->sabit = 0; tb->v = malloc(gur[i].n * 4);
        memcpy(tb->v, gur[i].v, gur[i].n * 4);
        s.dt = OD_F32; s.nd = gur[i].nd; s.n = gur[i].n; for (int j = 0; j < s.nd; j++) s.d[j] = gur[i].d[j]; s.b = tb;
        *o = s; return 0;
    }
    (void)m; return -1;
}
static void izle(oses_t *m, int di, void *b) {
    (void)b;
    oses_dugum_t *d = &m->dugum[di];
    for (int k = 0; k < d->nout; k++) {
        oses_tensor_t *t = &m->t[d->out[k]];
        if (!t->b) continue;
        int32_t id = d->out[k]; fwrite(&id, 4, 1, dok);
        uint8_t dt = t->dt, nd = t->nd; fwrite(&dt, 1, 1, dok); fwrite(&nd, 1, 1, dok);
        fwrite(t->d, 8, nd, dok);
        double s = 0, sa = 0, mx = 0;
        for (int64_t i = 0; i < t->n; i++) {
            double v = dt == OD_F32 ? ((float *)t->b->v)[i] : dt == OD_I64 ? (double)((int64_t *)t->b->v)[i] : ((uint8_t *)t->b->v)[i];
            s += v; sa += v < 0 ? -v : v; if ((v < 0 ? -v : v) > mx) mx = v < 0 ? -v : v;
        }
        fwrite(&s, 8, 1, dok); fwrite(&sa, 8, 1, dok); fwrite(&mx, 8, 1, dok);
        uint8_t tam = t->n <= 20000; fwrite(&tam, 1, 1, dok);
        if (tam) fwrite(t->b->v, dt == OD_F32 ? 4 : dt == OD_I64 ? 8 : 1, t->n, dok);
    }
}
static void *dosya(const char *y, long *n) {
    FILE *f = fopen(y, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    void *p = malloc(*n); if (fread(p, 1, *n, f) != (size_t)*n) return 0; fclose(f); return p;
}
int main(int argc, char **argv) {
    long mb, ib;
    uint8_t *mv = dosya(argv[1], &mb);
    int64_t *ids = dosya(argv[2], &ib);
    if (!mv || !ids) { fprintf(stderr, "dosya yok\n"); return 2; }
    oses_t m;
    oses_bellek_t bel = { ayir, birak };
    if (oses_yukle(&m, mv, mb, bel) != 0) { fprintf(stderr, "yukle: %s\n", m.hata); return 1; }
    if (argc > 3 && strcmp(argv[3], "-")) {
        long gb; uint8_t *g = dosya(argv[3], &gb), *p = g;
        while (p < g + gb) {
            gur_t *x = &gur[gsay++];
            x->id = *(uint32_t *)p; x->n = *(uint32_t *)(p + 4); p += 8;
            x->v = (float *)p; p += 4 * x->n;
            x->nd = *p++; memcpy(x->d, p, 8 * x->nd); p += 8 * x->nd;
        }
        m.gurultu = gurultu;
    }
    if (argc > 4) { dok = fopen(argv[4], "wb"); m.izle = izle; }
    if (getenv("BAS_EN_AZ")) m.bas_en_az = atoi(getenv("BAS_EN_AZ"));
    float og = 0.667f, ou = 1.0f, ow = 0.8f;
    if (getenv("OLCEK")) sscanf(getenv("OLCEK"), "%f,%f,%f", &og, &ou, &ow);
    float *ses; struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    long n = oses_calistir(&m, ids, (int)(ib / 8), og, ou, ow, &ses);
    clock_gettime(CLOCK_MONOTONIC, &b);
    if (n < 0) { fprintf(stderr, "calistir: %s\n", m.hata); return 1; }
    fprintf(stderr, "ornek=%ld sure=%.2f sn (ses %.2f sn)\n", n, (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9, n / 22050.0);
    fwrite(ses, 4, n, stdout);
    if (dok) fclose(dok);
    oses_birak(&m);
    return 0;
}
