/* US1 eğitici — bkz. us1_egit.h. egit.py ile satır satır eşleşir. */
#include "us1_egit.h"

/* ---------------- libm'siz matematik ---------------- */
static double e_exp(double x) {
    if (x > 700.0) x = 700.0;
    if (x < -745.0) return 0.0;
    double kd = x * 1.4426950408889634;
    long k = (long)(kd < 0 ? kd - 0.5 : kd + 0.5);
    double r = (x - (double)k * 6.93147180369123816490e-01) - (double)k * 1.90821492927058770002e-10;
    double t = 1.0 + r * (1.0 + r * (1.0/2 + r * (1.0/6 + r * (1.0/24 + r * (1.0/120 + r * (1.0/720 +
               r * (1.0/5040 + r * (1.0/40320 + r * (1.0/362880 + r * (1.0/3628800 + r * (1.0/39916800 +
               r * (1.0/479001600))))))))))));
    union { double d; uint64_t u; } s;
    if (k < -1022) { s.u = (uint64_t)(k + 1022 + 1023) << 52; return t * s.d * 2.2250738585072014e-308; }
    s.u = (uint64_t)(k + 1023) << 52;
    return t * s.d;
}
static double e_log(double x) {
    if (!(x > 0)) return -1e300;
    union { double d; uint64_t u; } v = { x };
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    if (e == -1023) { v.d = x * 4503599627370496.0; e = (int)((v.u >> 52) & 0x7FF) - 1023 - 52; }
    v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    double m = v.d;
    if (m > 1.4142135623730951) { m *= 0.5; e++; }
    double s = (m - 1.0) / (m + 1.0), s2 = s * s, sum = 0, p = s;
    for (int i = 1; i < 60; i += 2) { sum += p / i; p *= s2; if (p < 1e-20 && p > -1e-20) break; }
    return 2.0 * sum + e * 6.93147180369123816490e-01 + e * 1.90821492927058770002e-10;
}
static double e_sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
/* yarıda çifte (numpy rint). SSE4.1'in roundss'i KULLANILMAZ: eski
 * işlemcide geçersiz buyruk olurdu. |x| < 2^23: 2^23 ekle/çıkar hilesi
 * (varsayılan yuvarlama kipi en yakına-çifte). */
static float e_rint(float x) {
    volatile float k = 8388608.0f;
    if (x >= 8388608.0f || x <= -8388608.0f) return x;
    if (x >= 0) { volatile float t = x + k; return t - k; }
    volatile float t = x - k; return t + k;
}

static void sifirla(float *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = 0.0f; }
static float f32_oku(const uint8_t *p) { union { uint32_t u; float f; } v; v.u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); return v.f; }
static uint32_t u32_oku(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void u32_yaz(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* tensör boyları (satır, sütun) — model_yaz sırası */
static void boy_al(const us1e_t *e, int sira, int *r, int *c) {
    /* sira: 0 gomme, 1.. 9*L katman, sonra son_norm, cikis */
    if (sira == 0) { *r = e->V; *c = e->d; return; }
    int s = sira - 1;
    if (s < 9 * e->L) {
        switch (s % 9) {
        case 0: case 5: *r = 1; *c = e->d; return;           /* n1, n2 */
        case 1: case 2: case 3: case 4: *r = e->d; *c = e->d; return;
        case 6: case 8: *r = e->F; *c = e->d; return;        /* w1, w3 */
        default: *r = e->d; *c = e->F; return;               /* w2 */
        }
    }
    if (s == 9 * e->L) { *r = 1; *c = e->d; return; }       /* son_norm */
    *r = e->V; *c = e->d;                                    /* cikis */
}
static int tensor_sayisi(const us1e_t *e) { return 2 + 9 * e->L + 1; }

int us1e_kur(us1e_t *e, const uint8_t *f, uint32_t boy, us1e_ayir_fn ayir) {
    if (boy < 48 || f[0] != 'U' || f[1] != 'S' || f[2] != '1' || f[3] != 0) return -1;
    if (u32_oku(f + 4) != 1) return -1;                      /* yalnız v1 (RoPE yok) */
    e->d = (int)u32_oku(f + 8); e->L = (int)u32_oku(f + 12); e->H = (int)u32_oku(f + 16);
    e->F = (int)u32_oku(f + 20); e->V = (int)u32_oku(f + 24); e->T = (int)u32_oku(f + 28);
    if (e->d <= 0 || e->d > 256 || e->L <= 0 || e->L > 8 || e->H <= 0 || e->d % e->H || e->F <= 0 || e->F > 1024 ||
        e->V <= 0 || e->V > 256 || e->T <= 0 || e->T > 256) return -1;
    /* ofsetler */
    size_t o = 0; int r, c, s = 0;
    boy_al(e, s++, &r, &c); e->o_gomme = o; o += (size_t)r * c;
    for (int l = 0; l < e->L; l++) for (int k = 0; k < 9; k++) { boy_al(e, s++, &r, &c); e->o_kat[l][k] = o; o += (size_t)r * c; }
    boy_al(e, s++, &r, &c); e->o_son = o; o += (size_t)r * c;
    boy_al(e, s++, &r, &c); e->o_cikis = o; o += (size_t)r * c;
    int T = e->T, d = e->d, F = e->F, H = e->H, V = e->V;
    size_t kat = (size_t)T * d * 8 + (size_t)T * 2 + (size_t)H * T * T + (size_t)T * F * 4;
    size_t ara_n = kat * e->L + (size_t)T * d * 2 + T + (size_t)T * V
             + (size_t)T * d * 8 + (size_t)T * F * 3 + (size_t)T * T + (size_t)T * V;
    if (!ayir) {                                  /* tazele: önceki tamponlar, boylar aynı olmalı */
        if (!e->w || e->n != o || e->ara_n != ara_n) return -1;
    } else {
        e->n = o; e->ara_n = ara_n;
        e->w = ayir(o * 4); e->g = ayir(o * 4); e->m1 = ayir(o * 4); e->m2 = ayir(o * 4);
        e->ara = ayir(e->ara_n * 4);
    }
    if (!e->w || !e->g || !e->m1 || !e->m2 || !e->ara) return -1;
    sifirla(e->g, o); sifirla(e->m1, o); sifirla(e->m2, o);
    e->adim = 0;
    /* Q8 -> float */
    uint32_t p = 48;
    for (int t = 0; t < tensor_sayisi(e); t++) {
        if (p + 8 > boy) return -1;
        uint32_t sr = u32_oku(f + p), sc = u32_oku(f + p + 4); p += 8;
        boy_al(e, t, &r, &c);
        if ((int)sr != r || (int)sc != c) return -1;
        size_t taban = t == 0 ? e->o_gomme : t <= 9 * e->L ? e->o_kat[(t - 1) / 9][(t - 1) % 9]
                     : t == 9 * e->L + 1 ? e->o_son : e->o_cikis;
        for (int i = 0; i < r; i++) {
            if (p + 4 + (uint32_t)c > boy) return -1;
            float olc = f32_oku(f + p); p += 4;
            for (int j = 0; j < c; j++) e->w[taban + (size_t)i * c + j] = (float)(int8_t)f[p + j] * olc;
            p += (uint32_t)c;
        }
    }
    return 0;
}

/* ofset ve ara bölge boyları (us1e_kur ile aynı hesap) */
static int duzen_kur(us1e_t *e, us1e_ayir_fn ayir) {
    size_t o = 0; int r, c, s = 0;
    boy_al(e, s++, &r, &c); e->o_gomme = o; o += (size_t)r * c;
    for (int l = 0; l < e->L; l++) for (int k = 0; k < 9; k++) { boy_al(e, s++, &r, &c); e->o_kat[l][k] = o; o += (size_t)r * c; }
    boy_al(e, s++, &r, &c); e->o_son = o; o += (size_t)r * c;
    boy_al(e, s++, &r, &c); e->o_cikis = o; o += (size_t)r * c;
    int T = e->T, d = e->d, F = e->F, H = e->H, V = e->V;
    size_t kat = (size_t)T * d * 8 + (size_t)T * 2 + (size_t)H * T * T + (size_t)T * F * 4;
    e->n = o;
    e->ara_n = kat * e->L + (size_t)T * d * 2 + T + (size_t)T * V
             + (size_t)T * d * 8 + (size_t)T * F * 3 + (size_t)T * T + (size_t)T * V;
    e->w = ayir(o * 4); e->g = ayir(o * 4); e->m1 = ayir(o * 4); e->m2 = ayir(o * 4);
    e->ara = ayir(e->ara_n * 4);
    if (!e->w || !e->g || !e->m1 || !e->m2 || !e->ara) return -1;
    sifirla(e->w, o); sifirla(e->g, o); sifirla(e->m1, o); sifirla(e->m2, o);
    e->adim = 0;
    return 0;
}

static uint32_t g_tohum;
static float normal(void) {             /* 12 tekdüze toplamı - 6 ~ N(0,1) */
    float s = 0.0f;
    for (int i = 0; i < 12; i++) { g_tohum ^= g_tohum << 13; g_tohum ^= g_tohum >> 17; g_tohum ^= g_tohum << 5; s += (float)(g_tohum >> 8) / 16777216.0f; }
    return s - 6.0f;
}

int us1e_buyut(us1e_t *y, const us1e_t *x, int yL, int yF, us1e_ayir_fn ayir, uint32_t tohum) {
    if (yL < x->L || yF < x->F || yL > 8 || yF > 1024) return -1;
    *y = *x;
    y->L = yL; y->F = yF;
    if (duzen_kur(y, ayir) != 0) return -1;
    g_tohum = tohum ? tohum : 0x5EEDu;
    int d = x->d, F0 = x->F;
    float od = (float)(1.0 / e_sqrt((double)d)), oF = (float)(1.0 / e_sqrt((double)yF));
    for (int i = 0; i < x->V * d; i++) y->w[y->o_gomme + i] = x->w[x->o_gomme + i];
    for (int i = 0; i < d; i++) y->w[y->o_son + i] = x->w[x->o_son + i];
    for (int i = 0; i < x->V * d; i++) y->w[y->o_cikis + i] = x->w[x->o_cikis + i];
    for (int l = 0; l < yL; l++) {
        float *n1 = y->w + y->o_kat[l][0], *wq = y->w + y->o_kat[l][1], *wk = y->w + y->o_kat[l][2],
              *wv = y->w + y->o_kat[l][3], *wo = y->w + y->o_kat[l][4], *n2 = y->w + y->o_kat[l][5],
              *w1 = y->w + y->o_kat[l][6], *w2 = y->w + y->o_kat[l][7], *w3 = y->w + y->o_kat[l][8];
        if (l < x->L) {                                  /* eski katman: kopya + ffn genişlemesi */
            for (int k = 0; k < 6; k++) {
                int r_, c_; boy_al(x, 1 + 9 * l + k, &r_, &c_);
                for (int i = 0; i < r_ * c_; i++) y->w[y->o_kat[l][k] + i] = x->w[x->o_kat[l][k] + i];
            }
            const float *x1 = x->w + x->o_kat[l][6], *x2 = x->w + x->o_kat[l][7], *x3 = x->w + x->o_kat[l][8];
            for (int i = 0; i < yF; i++) for (int j = 0; j < d; j++) {
                w1[i * d + j] = i < F0 ? x1[i * d + j] : normal() * od;
                w3[i * d + j] = i < F0 ? x3[i * d + j] : normal() * od;
            }
            for (int i = 0; i < d; i++) for (int j = 0; j < yF; j++)
                w2[i * yF + j] = j < F0 ? x2[i * F0 + j] : 0.0f;    /* yeni birimin çıkışı SIFIR */
        } else {                                         /* yeni katman: artık yolu kimlik */
            for (int i = 0; i < d; i++) { n1[i] = 1.0f; n2[i] = 1.0f; }
            for (int i = 0; i < d * d; i++) { wq[i] = normal() * od; wk[i] = normal() * od; wv[i] = normal() * od; wo[i] = 0.0f; }
            for (int i = 0; i < yF * d; i++) { w1[i] = normal() * od; w3[i] = normal() * od; }
            for (int i = 0; i < d * yF; i++) w2[i] = 0.0f;
            (void)oF;
        }
    }
    return 0;
}

size_t us1e_dosya_boy(const us1e_t *e) {
    size_t n = 48; int r, c;
    for (int t = 0; t < tensor_sayisi(e); t++) { boy_al(e, t, &r, &c); n += 8 + (size_t)r * (4 + c); }
    return n;
}

long us1e_yaz(const us1e_t *e, uint8_t *f, size_t azami) {
    size_t n = us1e_dosya_boy(e);
    if (n > azami) return -1;
    for (int i = 0; i < 48; i++) f[i] = 0;
    f[0] = 'U'; f[1] = 'S'; f[2] = '1'; f[3] = 0;
    u32_yaz(f + 4, 1); u32_yaz(f + 8, (uint32_t)e->d); u32_yaz(f + 12, (uint32_t)e->L); u32_yaz(f + 16, (uint32_t)e->H);
    u32_yaz(f + 20, (uint32_t)e->F); u32_yaz(f + 24, (uint32_t)e->V); u32_yaz(f + 28, (uint32_t)e->T);
    size_t p = 48; int r, c;
    for (int t = 0; t < tensor_sayisi(e); t++) {
        boy_al(e, t, &r, &c);
        size_t taban = t == 0 ? e->o_gomme : t <= 9 * e->L ? e->o_kat[(t - 1) / 9][(t - 1) % 9]
                     : t == 9 * e->L + 1 ? e->o_son : e->o_cikis;
        u32_yaz(f + p, (uint32_t)r); u32_yaz(f + p + 4, (uint32_t)c); p += 8;
        for (int i = 0; i < r; i++) {
            const float *w = e->w + taban + (size_t)i * c;
            float mx = 0.0f;
            for (int j = 0; j < c; j++) { float a = w[j] < 0 ? -w[j] : w[j]; if (a > mx) mx = a; }
            float olc = mx / 127.0f;
            if (olc == 0.0f) olc = 1e-8f;
            union { float f; uint32_t u; } v; v.f = olc; u32_yaz(f + p, v.u); p += 4;
            for (int j = 0; j < c; j++) {
                float q = e_rint(w[j] / olc);
                if (q > 127.0f) q = 127.0f;
                if (q < -127.0f) q = -127.0f;
                f[p + j] = (uint8_t)(int8_t)(int)q;
            }
            p += (size_t)c;
        }
    }
    return (long)p;
}

/* y[T][r] = x[T][c] @ W[r][c]^T */
static void carp_t(float *y, const float *x, const float *W, int T, int r, int c) {
    for (int t = 0; t < T; t++) {
        const float *xt = x + (size_t)t * c;
        for (int i = 0; i < r; i++) {
            const float *wi = W + (size_t)i * c;
            float s = 0.0f;
            for (int j = 0; j < c; j++) s += xt[j] * wi[j];
            y[(size_t)t * r + i] = s;
        }
    }
}
/* y[T][c] += dy[T][r] @ W[r][c] */
static void carp_ekle(float *y, const float *dy, const float *W, int T, int r, int c) {
    for (int t = 0; t < T; t++) {
        float *yt = y + (size_t)t * c;
        const float *dt = dy + (size_t)t * r;
        for (int i = 0; i < r; i++) {
            float a = dt[i];
            if (a == 0.0f) continue;
            const float *wi = W + (size_t)i * c;
            for (int j = 0; j < c; j++) yt[j] += a * wi[j];
        }
    }
}
/* G[r][c] += dy[T][r]^T @ x[T][c] */
static void dis_ekle(float *G, const float *dy, const float *x, int T, int r, int c) {
    for (int t = 0; t < T; t++) {
        const float *dt = dy + (size_t)t * r, *xt = x + (size_t)t * c;
        for (int i = 0; i < r; i++) {
            float a = dt[i];
            if (a == 0.0f) continue;
            float *gi = G + (size_t)i * c;
            for (int j = 0; j < c; j++) gi[j] += a * xt[j];
        }
    }
}
static void rms(const float *x, float *r, float *h, const float *n, int T, int d) {
    for (int t = 0; t < T; t++) {
        double s = 0; for (int j = 0; j < d; j++) s += (double)x[t * d + j] * x[t * d + j];
        float rr = (float)e_sqrt(s / d + 1e-5);
        r[t] = rr;
        for (int j = 0; j < d; j++) h[t * d + j] = x[t * d + j] / rr * n[j];
    }
}
/* dx += dn/r - x/r^3 * mean(dn*x); dn = dh * n */
static void rms_geri(float *dx, const float *dh, const float *n, const float *x, const float *r, int T, int d, float *gn) {
    for (int t = 0; t < T; t++) {
        float rr = r[t];
        double m = 0;
        for (int j = 0; j < d; j++) {
            float dn = dh[t * d + j] * n[j];
            m += (double)dn * x[t * d + j];
            gn[j] += dh[t * d + j] * (x[t * d + j] / rr);
        }
        float mf = (float)(m / d), r3 = rr * rr * rr;
        for (int j = 0; j < d; j++) {
            float dn = dh[t * d + j] * n[j];
            dx[t * d + j] += dn / rr - (x[t * d + j] / r3) * mf;
        }
    }
}

/* ara bölge düzeni */
typedef struct {
    float *girdi, *rms1, *h1, *q, *k, *v, *p, *cik, *orta, *rms2, *h2, *g1, *g3, *sig, *sg;
} katara_t;

static double gecis(us1e_t *e, const int *x, const int *y, const float *aw, int T, double wt, int geri) {
    int d = e->d, F = e->F, H = e->H, V = e->V, bs = d / H, L = e->L;
    float *a = e->ara;
    katara_t K[8];
    for (int l = 0; l < L; l++) {
        K[l].girdi = a; a += T * d; K[l].rms1 = a; a += T; K[l].h1 = a; a += T * d;
        K[l].q = a; a += T * d; K[l].k = a; a += T * d; K[l].v = a; a += T * d;
        K[l].p = a; a += H * T * T; K[l].cik = a; a += T * d; K[l].orta = a; a += T * d;
        K[l].rms2 = a; a += T; K[l].h2 = a; a += T * d;
        K[l].g1 = a; a += T * F; K[l].g3 = a; a += T * F; K[l].sig = a; a += T * F; K[l].sg = a; a += T * F;
    }
    float *son_girdi = a; a += T * d; float *rmsS = a; a += T; float *hS = a; a += T * d; float *mantik = a; a += T * V;
    float *X = a; a += T * d;                       /* akış */
    float *dx = a; a += T * d; float *dh = a; a += T * d; float *dcik = a; a += T * d;
    float *dq = a; a += T * d; float *dk = a; a += T * d; float *dv = a; a += T * d; float *tmp = a; a += T * d;
    float *du = a; a += T * F; float *dg1 = a; a += T * F; float *dg3 = a; a += T * F;
    float *dp = a; a += T * T; float *dl = a; a += T * V;
    (void)tmp;
    const float olcek = (float)(1.0 / e_sqrt((double)bs));

    for (int t = 0; t < T; t++) for (int j = 0; j < d; j++) X[t * d + j] = e->w[e->o_gomme + (size_t)x[t] * d + j];
    for (int l = 0; l < L; l++) {
        katara_t *k = &K[l];
        const float *n1 = e->w + e->o_kat[l][0], *wq = e->w + e->o_kat[l][1], *wk = e->w + e->o_kat[l][2],
                    *wv = e->w + e->o_kat[l][3], *wo = e->w + e->o_kat[l][4], *n2 = e->w + e->o_kat[l][5],
                    *w1 = e->w + e->o_kat[l][6], *w2 = e->w + e->o_kat[l][7], *w3 = e->w + e->o_kat[l][8];
        for (int i = 0; i < T * d; i++) k->girdi[i] = X[i];
        rms(X, k->rms1, k->h1, n1, T, d);
        carp_t(k->q, k->h1, wq, T, d, d); carp_t(k->k, k->h1, wk, T, d, d); carp_t(k->v, k->h1, wv, T, d, d);
        for (int i = 0; i < T * d; i++) k->cik[i] = 0.0f;
        for (int hh = 0; hh < H; hh++) {
            float *P = k->p + (size_t)hh * T * T;
            for (int i = 0; i < T; i++) {
                float mx = -3.0e38f;
                for (int j = 0; j <= i; j++) {
                    float s = 0.0f;
                    for (int z = 0; z < bs; z++) s += k->q[i * d + hh * bs + z] * k->k[j * d + hh * bs + z];
                    s *= olcek;
                    P[i * T + j] = s; if (s > mx) mx = s;
                }
                double top = 0;
                for (int j = 0; j <= i; j++) { float ex = (float)e_exp((double)(P[i * T + j] - mx)); P[i * T + j] = ex; top += ex; }
                float tf = (float)top;
                for (int j = 0; j <= i; j++) P[i * T + j] /= tf;
                for (int j = i + 1; j < T; j++) P[i * T + j] = 0.0f;
                for (int j = 0; j <= i; j++) {
                    float pij = P[i * T + j];
                    for (int z = 0; z < bs; z++) k->cik[i * d + hh * bs + z] += pij * k->v[j * d + hh * bs + z];
                }
            }
        }
        carp_t(k->orta, k->cik, wo, T, d, d);           /* cik @ wo^T (geçici) */
        for (int i = 0; i < T * d; i++) { X[i] = k->girdi[i] + k->orta[i]; k->orta[i] = X[i]; }
        rms(X, k->rms2, k->h2, n2, T, d);
        carp_t(k->g1, k->h2, w1, T, F, d); carp_t(k->g3, k->h2, w3, T, F, d);
        for (int i = 0; i < T * F; i++) {
            k->sig[i] = (float)(1.0 / (1.0 + e_exp(-(double)k->g1[i])));
            k->sg[i] = k->g1[i] * k->sig[i];
            du[i] = k->sg[i] * k->g3[i];                 /* u (geçici) */
        }
        carp_t(dh, du, w2, T, d, F);                   /* (sg*g3) @ w2^T */
        for (int i = 0; i < T * d; i++) X[i] = k->orta[i] + dh[i];
    }
    for (int i = 0; i < T * d; i++) son_girdi[i] = X[i];
    rms(X, rmsS, hS, e->w + e->o_son, T, d);
    carp_t(mantik, hS, e->w + e->o_cikis, T, V, d);

    /* kayıp */
    double kayip = 0;
    for (int t = 0; t < T; t++) {
        float *z = mantik + (size_t)t * V;
        float mx = z[0]; for (int i = 1; i < V; i++) if (z[i] > mx) mx = z[i];
        double top = 0;
        for (int i = 0; i < V; i++) { float ex = (float)e_exp((double)(z[i] - mx)); dl[t * V + i] = ex; top += ex; }
        float tf = (float)top;
        for (int i = 0; i < V; i++) dl[t * V + i] /= tf;
        float wv_ = aw ? aw[t] : 1.0f;
        kayip += -e_log((double)dl[t * V + y[t]] + 1e-12) * wv_;
        dl[t * V + y[t]] -= 1.0f;
        float c = (float)(wv_ / wt);
        for (int i = 0; i < V; i++) dl[t * V + i] *= c;
    }
    if (!geri) return kayip;

    float *G = e->g;
    dis_ekle(G + e->o_cikis, dl, hS, T, V, d);
    for (int i = 0; i < T * d; i++) dh[i] = 0.0f;
    carp_ekle(dh, dl, e->w + e->o_cikis, T, V, d);     /* dl @ cikis */
    for (int i = 0; i < T * d; i++) dx[i] = 0.0f;
    rms_geri(dx, dh, e->w + e->o_son, son_girdi, rmsS, T, d, G + e->o_son);

    for (int l = L - 1; l >= 0; l--) {
        katara_t *k = &K[l];
        const float *n1 = e->w + e->o_kat[l][0], *wq = e->w + e->o_kat[l][1], *wk = e->w + e->o_kat[l][2],
                    *wv = e->w + e->o_kat[l][3], *wo = e->w + e->o_kat[l][4], *n2 = e->w + e->o_kat[l][5],
                    *w1 = e->w + e->o_kat[l][6], *w2 = e->w + e->o_kat[l][7], *w3 = e->w + e->o_kat[l][8];
        /* u = sg*g3 */
        for (int i = 0; i < T * F; i++) dg1[i] = k->sg[i] * k->g3[i];
        dis_ekle(G + e->o_kat[l][7], dx, dg1, T, d, F);           /* gw2 = dx^T u */
        for (int i = 0; i < T * F; i++) du[i] = 0.0f;
        carp_ekle(du, dx, w2, T, d, F);                           /* du = dx @ w2 */
        for (int i = 0; i < T * F; i++) {
            float dsg = du[i] * k->g3[i];
            dg3[i] = du[i] * k->sg[i];
            float s = k->sig[i];
            dg1[i] = dsg * (s * (1.0f + k->g1[i] * (1.0f - s)));
        }
        dis_ekle(G + e->o_kat[l][6], dg1, k->h2, T, F, d);
        dis_ekle(G + e->o_kat[l][8], dg3, k->h2, T, F, d);
        for (int i = 0; i < T * d; i++) dh[i] = 0.0f;
        carp_ekle(dh, dg1, w1, T, F, d);
        carp_ekle(dh, dg3, w3, T, F, d);
        rms_geri(dx, dh, n2, k->orta, k->rms2, T, d, G + e->o_kat[l][5]);

        dis_ekle(G + e->o_kat[l][4], dx, k->cik, T, d, d);        /* gwo */
        for (int i = 0; i < T * d; i++) { dcik[i] = 0.0f; dq[i] = 0.0f; dk[i] = 0.0f; dv[i] = 0.0f; }
        carp_ekle(dcik, dx, wo, T, d, d);                         /* dcik = dx @ wo */
        for (int hh = 0; hh < H; hh++) {
            float *P = k->p + (size_t)hh * T * T;
            for (int i = 0; i < T; i++) {
                double sat = 0;
                for (int j = 0; j <= i; j++) {
                    float s = 0.0f;
                    for (int z = 0; z < bs; z++) s += dcik[i * d + hh * bs + z] * k->v[j * d + hh * bs + z];
                    dp[i * T + j] = s;
                    sat += (double)s * P[i * T + j];
                }
                float satf = (float)sat;
                for (int j = 0; j <= i; j++) {
                    float pij = P[i * T + j];
                    /* dv_j += p_ij dcik_i */
                    for (int z = 0; z < bs; z++) dv[j * d + hh * bs + z] += pij * dcik[i * d + hh * bs + z];
                    float ds = pij * (dp[i * T + j] - satf) * olcek;
                    for (int z = 0; z < bs; z++) {
                        dq[i * d + hh * bs + z] += ds * k->k[j * d + hh * bs + z];
                        dk[j * d + hh * bs + z] += ds * k->q[i * d + hh * bs + z];
                    }
                }
            }
        }
        dis_ekle(G + e->o_kat[l][1], dq, k->h1, T, d, d);
        dis_ekle(G + e->o_kat[l][2], dk, k->h1, T, d, d);
        dis_ekle(G + e->o_kat[l][3], dv, k->h1, T, d, d);
        for (int i = 0; i < T * d; i++) dh[i] = 0.0f;
        carp_ekle(dh, dq, wq, T, d, d);
        carp_ekle(dh, dk, wk, T, d, d);
        carp_ekle(dh, dv, wv, T, d, d);
        rms_geri(dx, dh, n1, k->girdi, k->rms1, T, d, G + e->o_kat[l][0]);
    }
    for (int t = 0; t < T; t++) for (int j = 0; j < d; j++) G[e->o_gomme + (size_t)x[t] * d + j] += dx[t * d + j];
    return kayip;
}

double us1e_ileri_geri(us1e_t *e, const int *x, const int *y, const float *a, int T, double wt) {
    if (T > e->T) T = e->T;
    return gecis(e, x, y, a, T, wt, 1);
}
double us1e_kayip(us1e_t *e, const int *x, const int *y, const float *a, int T) {
    if (T > e->T) T = e->T;
    return gecis(e, x, y, a, T, 1.0, 0);
}

float us1e_oran(int t, int isinma, int plan, float tepe) {
    if (t <= isinma) return tepe * (float)t / (float)isinma;
    double f = (double)(t - isinma) / (double)(plan - isinma > 1 ? plan - isinma : 1);
    /* cos(pi f), f in [0,1]: kendi serisi */
    double x = 3.141592653589793 * f, c = 0, terim = 1, x2 = x * x;
    for (int k = 0; k < 30; k++) { c += terim; terim *= -x2 / ((2 * k + 1) * (2 * k + 2)); }
    return (float)(tepe * (0.1 + 0.9 * 0.5 * (1 + c)));
}

/* 1.5: ince ayar kipi — SGD (momentum 0.9). Adam'ın taze momentleri, eğitimi
 * bitmiş bir modelde gradyanı küçük parametreleri bile her adımda ~lr kadar
 * oynatıyor (ölçüldü: bildiği çiftleri unutuyordu). SGD yalnız gradyanı
 * olanı ve onunla orantılı oynatır. Sönüm yok. */
double us1e_adim_sgd(us1e_t *e, float lr) {
    double nn = 0;
    for (size_t i = 0; i < e->n; i++) nn += (double)e->g[i] * e->g[i];
    double norm = e_sqrt(nn);
    float olc = (float)(1.0 / (norm + 1e-6)); if (olc > 1.0f) olc = 1.0f;
    e->adim++;
    for (size_t i = 0; i < e->n; i++) {
        float m = e->m1[i] * 0.9f + e->g[i] * olc;
        e->m1[i] = m;
        e->w[i] -= lr * m;
        e->g[i] = 0.0f;
    }
    return norm;
}

double us1e_adim(us1e_t *e, float lr) {
    double nn = 0;
    for (size_t i = 0; i < e->n; i++) nn += (double)e->g[i] * e->g[i];
    double norm = e_sqrt(nn);
    float olc = (float)(1.0 / (norm + 1e-6)); if (olc > 1.0f) olc = 1.0f;
    e->adim++;
    int t = e->adim;
    double b1t = 1.0, b2t = 1.0;
    for (int i = 0; i < t; i++) { b1t *= 0.9; b2t *= 0.99; }
    float d1 = (float)(1.0 - b1t), d2 = (float)(1.0 - b2t);
    float sonum_oran = (float)((double)lr * 0.01);     /* numpy: lr*0.01 (float64) sonra float32 */
    /* matris mi (ağırlık sönümü): gomme HARİÇ, n1/n2/son_norm HARİÇ */
    for (int s = 0; s < tensor_sayisi(e); s++) {
        int r, c; boy_al(e, s, &r, &c);
        size_t taban = s == 0 ? e->o_gomme : s <= 9 * e->L ? e->o_kat[(s - 1) / 9][(s - 1) % 9]
                     : s == 9 * e->L + 1 ? e->o_son : e->o_cikis;
        int sonum = (r > 1) && s > 0;
        size_t n = (size_t)r * c;
        for (size_t i = 0; i < n; i++) {
            float gr = e->g[taban + i] * olc;
            float m = e->m1[taban + i] * 0.9f; m += 0.1f * gr; e->m1[taban + i] = m;
            float v = e->m2[taban + i] * 0.99f; v += 0.01f * gr * gr; e->m2[taban + i] = v;
            float mh = m / d1, vh = v / d2;
            float p = e->w[taban + i];
            p -= lr * mh / ((float)e_sqrt((double)vh) + 1e-8f);
            if (sonum) p -= sonum_oran * p;
            e->w[taban + i] = p;
            e->g[taban + i] = 0.0f;
        }
    }
    return norm;
}
