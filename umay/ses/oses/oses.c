/* =====================================================================
 * OSES — ÖTÜKEN ses motoru yorumlayıcısı (bkz. oses.h)
 * ===================================================================== */
#include "oses.h"
#include "oses_mat.h"

enum {
    O_Unsqueeze, O_Shape, O_Mul, O_Add, O_Slice, O_Reshape, O_Concat, O_Gather, O_Transpose,
    O_ConstantOfShape, O_Sub, O_Cast, O_Conv, O_Expand, O_Div, O_ReduceMean, O_Where, O_Equal,
    O_Pad, O_Range, O_Pow, O_Sqrt, O_ScatterND, O_MatMul, O_Erf, O_NonZero, O_GatherElements,
    O_Tanh, O_Sigmoid, O_LeakyRelu, O_GatherND, O_Squeeze, O_Softmax, O_Neg, O_Split, O_CumSum,
    O_Exp, O_Relu, O_GreaterOrEqual, O_ReduceSum, O_Less, O_LessOrEqual, O_And, O_Not,
    O_Softplus, O_ConvTranspose, O_RandomNormalLike, O_Ceil, O_Clip, O_ReduceMax, O_SAYI
};

#define HATA(m, s) do { oses_hata(m, s); return -1; } while (0)

static void kopya_dize(char *d, int boy, const char *s) { int i = 0; while (i < boy - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static void oses_hata(oses_t *m, const char *s) { if (!m->hata[0]) kopya_dize(m->hata, sizeof m->hata, s); }
static void bel_kopya(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; }
static void bel_sifir(void *d, size_t n) { uint8_t *a = d; while (n--) *a++ = 0; }

static size_t dt_boy(int dt) { return dt == OD_F32 ? 4 : dt == OD_I64 ? 8 : 1; }

/* ------------------------------------------------------------------ */
/* tensör yönetimi                                                     */
/* ------------------------------------------------------------------ */
static void tensor_birak(oses_t *m, oses_tensor_t *t) {
    if (t->b) {
        if (!t->b->sabit && --t->b->sayac == 0) { m->bel.birak(t->b->v); m->bel.birak(t->b); }
        else if (t->b->sabit) { /* sabit tampon: paylaşılan kabuk */ if (--t->b->sayac == 0) m->bel.birak(t->b); }
    }
    t->b = 0;
}

static int yeni(oses_t *m, oses_tensor_t *t, int dt, int nd, const int64_t *d) {
    tensor_birak(m, t);
    t->dt = (uint8_t)dt; t->nd = (uint8_t)nd; t->n = 1;
    for (int i = 0; i < nd; i++) { t->d[i] = d[i]; t->n *= d[i]; }
    if (t->n < 0) HATA(m, "negatif boyut");
    oses_tampon_t *b = m->bel.ayir(sizeof *b);
    if (!b) HATA(m, "bellek yok");
    size_t bayt = (size_t)t->n * dt_boy(dt);
    b->v = m->bel.ayir(bayt ? bayt : 8);
    if (!b->v) { m->bel.birak(b); HATA(m, "bellek yok (tensor)"); }
    b->sayac = 1; b->sabit = 0;
    t->b = b;
    return 0;
}

static int paylas(oses_t *m, oses_tensor_t *hedef, const oses_tensor_t *kaynak, int nd, const int64_t *d) {
    oses_tampon_t *b = kaynak->b;
    b->sayac++;
    tensor_birak(m, hedef);
    hedef->b = b; hedef->dt = kaynak->dt; hedef->nd = (uint8_t)nd; hedef->n = 1;
    for (int i = 0; i < nd; i++) { hedef->d[i] = d[i]; hedef->n *= d[i]; }
    if (hedef->n != kaynak->n) HATA(m, "paylasim boyu tutmuyor");
    return 0;
}

#define F(t) ((float *)(t)->b->v)
#define L(t) ((int64_t *)(t)->b->v)
#define B(t) ((uint8_t *)(t)->b->v)

static double deger(const oses_tensor_t *t, int64_t i) {
    if (t->dt == OD_F32) return F(t)[i];
    if (t->dt == OD_I64) return (double)L(t)[i];
    return B(t)[i];
}
static int64_t tam(const oses_tensor_t *t, int64_t i) {
    if (t->dt == OD_I64) return L(t)[i];
    if (t->dt == OD_F32) return (int64_t)F(t)[i];
    return B(t)[i];
}

/* ------------------------------------------------------------------ */
/* yayın (broadcast)                                                    */
/* ------------------------------------------------------------------ */
typedef struct { int nd; int64_t d[OSES_AZAMI_BOYUT]; int64_t ad[3][OSES_AZAMI_BOYUT]; int64_t n; } yayin_t;

/* k tensörün yayın biçimi; ad[i][j]: i. girişin j. boyuttaki adımı (0 = yayın) */
static int yayin_kur(oses_t *m, yayin_t *y, int k, const oses_tensor_t **g) {
    int nd = 0;
    for (int i = 0; i < k; i++) if (g[i]->nd > nd) nd = g[i]->nd;
    y->nd = nd; y->n = 1;
    for (int j = 0; j < nd; j++) {
        int64_t boy = 1;
        for (int i = 0; i < k; i++) {
            int o = j - (nd - g[i]->nd);
            int64_t b = o < 0 ? 1 : g[i]->d[o];
            if (b != 1) { if (boy != 1 && boy != b) HATA(m, "yayin uyumsuz"); boy = b; }
        }
        y->d[j] = boy; y->n *= boy;
    }
    for (int i = 0; i < k; i++) {
        int64_t adim = 1;
        for (int j = nd - 1; j >= 0; j--) {
            int o = j - (nd - g[i]->nd);
            int64_t b = o < 0 ? 1 : g[i]->d[o];
            y->ad[i][j] = (b == 1) ? 0 : adim;
            adim *= b;
        }
    }
    return 0;
}

/* sayaç ile yayın gezintisi: her çıktı öğesi için giriş ofsetleri */
#define YAYIN_DONGU(y, k, ...) do { \
    int64_t _say[OSES_AZAMI_BOYUT] = {0}, of[3] = {0, 0, 0}; \
    for (int64_t _i = 0; _i < (y).n; _i++) { \
        __VA_ARGS__; \
        for (int _j = (y).nd - 1; _j >= 0; _j--) { \
            _say[_j]++; for (int _q = 0; _q < (k); _q++) of[_q] += (y).ad[_q][_j]; \
            if (_say[_j] < (y).d[_j]) break; \
            for (int _q = 0; _q < (k); _q++) of[_q] -= (y).ad[_q][_j] * (y).d[_j]; \
            _say[_j] = 0; \
        } \
    } } while (0)

/* Yerinde hesap: giriş bu düğümde son kez okunuyorsa, sabit değilse ve
 * tamponu başkasıyla paylaşılmıyorsa çıktı onun tamponuna yazılır (öğe
 * öğe işlemlerde r[i] yalnız x[i]'ye bağlıdır). Kod çözücünün son
 * katlarında tepe belleği düşürür; sonuç bit bit aynıdır. */
static int devralinir(const oses_t *m, int di, int j) {
    const oses_dugum_t *dg = &m->dugum[di];
    int32_t id = dg->in[j];
    if (id < 0 || m->son_kullanim[id] != di) return 0;
    for (int k = 0; k < dg->nin; k++) if (k != j && dg->in[k] == id) return 0;
    const oses_tensor_t *t = &m->t[id];
    return t->b && !t->b->sabit && t->b->sayac == 1 && t->dt == OD_F32;
}

static int ikili(oses_t *m, int op, oses_tensor_t *a, oses_tensor_t *b, oses_tensor_t *o, int al_a, int al_b) {
    const oses_tensor_t *g[2] = { a, b };
    yayin_t y;
    if (yayin_kur(m, &y, 2, g)) return -1;
    int karsilastir = (op == O_Equal || op == O_Less || op == O_LessOrEqual || op == O_GreaterOrEqual || op == O_And);
    int dt = karsilastir ? OD_BOOL : a->dt;
    if (!karsilastir && a->dt != b->dt) HATA(m, "ikili islem tur uyumsuz");
    oses_tensor_t s = {0};
    /* hızlı yol: aynı biçimde float (girişlerden biri devralınabilir) */
    if (dt == OD_F32 && a->n == y.n && b->n == y.n && op != O_Pow &&
        (al_a || al_b) && (op == O_Mul || op == O_Add || op == O_Sub || op == O_Div)) {
        oses_tensor_t *k = al_a ? a : b;
        float *x = F(a), *z = F(b), *r = F(k);
        int64_t n = y.n;
        switch (op) {
        case O_Mul: for (int64_t i = 0; i < n; i++) r[i] = x[i] * z[i]; break;
        case O_Add: for (int64_t i = 0; i < n; i++) r[i] = x[i] + z[i]; break;
        case O_Sub: for (int64_t i = 0; i < n; i++) r[i] = x[i] - z[i]; break;
        default:    for (int64_t i = 0; i < n; i++) r[i] = x[i] / z[i]; break;
        }
        tensor_birak(m, o); *o = *k; o->nd = (uint8_t)y.nd;
        for (int i = 0; i < y.nd; i++) o->d[i] = y.d[i];
        k->b = 0; return 0;
    }
    if (yeni(m, &s, dt, y.nd, y.d)) return -1;
    if (dt == OD_F32 && a->n == y.n && b->n == y.n) {
        float *x = F(a), *z = F(b), *r = F(&s);
        int64_t n = y.n;
        switch (op) {
        case O_Mul: for (int64_t i = 0; i < n; i++) r[i] = x[i] * z[i]; break;
        case O_Add: for (int64_t i = 0; i < n; i++) r[i] = x[i] + z[i]; break;
        case O_Sub: for (int64_t i = 0; i < n; i++) r[i] = x[i] - z[i]; break;
        case O_Div: for (int64_t i = 0; i < n; i++) r[i] = x[i] / z[i]; break;
        case O_Pow: for (int64_t i = 0; i < n; i++) r[i] = (float)osm_pow(x[i], z[i]); break;
        default: goto genel;
        }
        tensor_birak(m, o); *o = s; return 0;
    }
genel:
    if (dt == OD_F32) {
        float *x = F(a), *z = F(b), *r = F(&s);
        switch (op) {
        case O_Mul: YAYIN_DONGU(y, 2, r[_i] = x[of[0]] * z[of[1]]); break;
        case O_Add: YAYIN_DONGU(y, 2, r[_i] = x[of[0]] + z[of[1]]); break;
        case O_Sub: YAYIN_DONGU(y, 2, r[_i] = x[of[0]] - z[of[1]]); break;
        case O_Div: YAYIN_DONGU(y, 2, r[_i] = x[of[0]] / z[of[1]]); break;
        case O_Pow: YAYIN_DONGU(y, 2, r[_i] = (float)osm_pow(x[of[0]], z[of[1]])); break;
        default: tensor_birak(m, &s); HATA(m, "f32 ikili islem yok");
        }
    } else if (dt == OD_I64) {
        int64_t *x = L(a), *z = L(b), *r = L(&s);
        switch (op) {
        case O_Mul: YAYIN_DONGU(y, 2, r[_i] = x[of[0]] * z[of[1]]); break;
        case O_Add: YAYIN_DONGU(y, 2, r[_i] = x[of[0]] + z[of[1]]); break;
        case O_Sub: YAYIN_DONGU(y, 2, r[_i] = x[of[0]] - z[of[1]]); break;
        case O_Div: YAYIN_DONGU(y, 2, r[_i] = z[of[1]] ? x[of[0]] / z[of[1]] : 0); break;
        default: tensor_birak(m, &s); HATA(m, "i64 ikili islem yok");
        }
    } else {
        uint8_t *r = B(&s);
        switch (op) {
        case O_Equal:        YAYIN_DONGU(y, 2, r[_i] = deger(a, of[0]) == deger(b, of[1])); break;
        case O_Less:         YAYIN_DONGU(y, 2, r[_i] = deger(a, of[0]) <  deger(b, of[1])); break;
        case O_LessOrEqual:  YAYIN_DONGU(y, 2, r[_i] = deger(a, of[0]) <= deger(b, of[1])); break;
        case O_GreaterOrEqual: YAYIN_DONGU(y, 2, r[_i] = deger(a, of[0]) >= deger(b, of[1])); break;
        case O_And:          YAYIN_DONGU(y, 2, r[_i] = B(a)[of[0]] && B(b)[of[1]]); break;
        default: tensor_birak(m, &s); HATA(m, "bool ikili islem yok");
        }
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int tekli(oses_t *m, int op, oses_tensor_t *a, oses_tensor_t *o, float alfa, int al) {
    oses_tensor_t s = {0};
    int dt = a->dt;
    if (op == O_Not) dt = OD_BOOL;
    if (al && dt == OD_F32) { s = *a; a->b = 0; s.b->sayac = 1; a = &s; }   /* yerinde */
    else if (yeni(m, &s, dt, a->nd, a->d)) return -1;
    int64_t n = a->n;
    if (op == O_Not) { for (int64_t i = 0; i < n; i++) B(&s)[i] = !B(a)[i]; }
    else if (dt == OD_I64) {
        if (op == O_Neg) for (int64_t i = 0; i < n; i++) L(&s)[i] = -L(a)[i];
        else if (op == O_Relu) for (int64_t i = 0; i < n; i++) L(&s)[i] = L(a)[i] > 0 ? L(a)[i] : 0;
        else { tensor_birak(m, &s); HATA(m, "i64 tekli islem yok"); }
    } else {
        float *x = F(a), *r = F(&s);
        switch (op) {
        case O_Sqrt:     for (int64_t i = 0; i < n; i++) r[i] = (float)osm_sqrt(x[i]); break;
        case O_Erf:      for (int64_t i = 0; i < n; i++) r[i] = (float)osm_erf(x[i]); break;
        case O_Tanh:     for (int64_t i = 0; i < n; i++) r[i] = (float)osm_tanh(x[i]); break;
        case O_Sigmoid:  for (int64_t i = 0; i < n; i++) r[i] = (float)(1.0 / (1.0 + osm_exp(-(double)x[i]))); break;
        case O_LeakyRelu:for (int64_t i = 0; i < n; i++) r[i] = x[i] >= 0 ? x[i] : x[i] * alfa; break;
        case O_Neg:      for (int64_t i = 0; i < n; i++) r[i] = -x[i]; break;
        case O_Exp:      for (int64_t i = 0; i < n; i++) r[i] = (float)osm_exp(x[i]); break;
        case O_Relu:     for (int64_t i = 0; i < n; i++) r[i] = x[i] > 0 ? x[i] : 0; break;
        case O_Softplus: for (int64_t i = 0; i < n; i++) r[i] = (float)osm_softplus(x[i]); break;
        case O_Ceil:     for (int64_t i = 0; i < n; i++) r[i] = (float)osm_ceil(x[i]); break;
        default: tensor_birak(m, &s); HATA(m, "f32 tekli islem yok");
        }
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

/* ------------------------------------------------------------------ */
/* yardımcılar                                                          */
/* ------------------------------------------------------------------ */
static int64_t eksen(int64_t a, int nd) { return a < 0 ? a + nd : a; }

/* genel boyut gezintisi için adımlar */
static void adimlar(const oses_tensor_t *t, int64_t *ad) {
    int64_t s = 1;
    for (int j = t->nd - 1; j >= 0; j--) { ad[j] = s; s *= t->d[j]; }
}

/* öğe kopyala (tür bağımsız) */
static void oge_kopya(void *d, int64_t di, const void *s, int64_t si, int dt) {
    if (dt == OD_F32) ((float *)d)[di] = ((const float *)s)[si];
    else if (dt == OD_I64) ((int64_t *)d)[di] = ((const int64_t *)s)[si];
    else ((uint8_t *)d)[di] = ((const uint8_t *)s)[si];
}

/* ------------------------------------------------------------------ */
/* işlemler                                                            */
/* ------------------------------------------------------------------ */
/* bir çıktı satırına (Lo) giriş satırının k. dokunuşunu ekleme sınırları */
static void conv_sinir(int64_t ofs, int64_t st, int64_t Lg, int64_t Lo, int64_t *l0, int64_t *l1) {
    *l0 = 0; *l1 = Lo;
    if (ofs < 0) *l0 = (-ofs + st - 1) / st;
    if (Lg - ofs <= 0) { *l1 = 0; return; }
    int64_t lmax = (Lg - 1 - ofs) / st + 1;
    if (lmax < *l1) *l1 = lmax;
}

static int op_conv(oses_t *m, oses_dugum_t *dg, oses_tensor_t *x, oses_tensor_t *w, oses_tensor_t *bias, oses_tensor_t *o) {
    if (x->nd != 3 || w->nd != 3) HATA(m, "conv boyut");
    int64_t N = x->d[0], C = x->d[1], Lg = x->d[2], M = w->d[0], Cg = w->d[1], K = w->d[2];
    int64_t dil = dg->ia[0], grup = dg->ia[1], pl = dg->ia[3], pr = dg->ia[4], st = dg->ia[5];
    if (Cg * grup != C) HATA(m, "conv grup");
    int64_t Lo = (Lg + pl + pr - dil * (K - 1) - 1) / st + 1;
    int64_t d[3] = { N, M, Lo };
    oses_tensor_t s = {0};
    if (yeni(m, &s, OD_F32, 3, d)) return -1;
    float *X = F(x), *W = F(w), *R = F(&s);
    int64_t Mg = M / grup;
    for (int64_t n = 0; n < N; n++)
        for (int64_t mm = 0; mm < M; ) {
            int64_t g = mm / Mg;
            /* aynı gruptan en çok 4 çıktı kanalı birlikte: giriş satırı bir kez okunur */
            int64_t blok = 4;
            if ((mm % Mg) + blok > Mg) blok = Mg - (mm % Mg);
            if (blok > M - mm) blok = M - mm;
            float *r[4];
            for (int64_t b = 0; b < blok; b++) {
                r[b] = R + (n * M + mm + b) * Lo;
                float bb = bias ? F(bias)[mm + b] : 0.0f;
                for (int64_t l = 0; l < Lo; l++) r[b][l] = bb;
            }
            for (int64_t c = 0; c < Cg; c++) {
                const float *xr = X + (n * C + g * Cg + c) * Lg;
                for (int64_t k = 0; k < K; k++) {
                    int64_t ofs = k * dil - pl, l0, l1;
                    conv_sinir(ofs, st, Lg, Lo, &l0, &l1);
                    if (l1 <= l0) continue;
                    if (st == 1) {
                        const float *xp = xr + ofs;
                        if (blok == 4) {
                            float w0 = W[((mm + 0) * Cg + c) * K + k], w1 = W[((mm + 1) * Cg + c) * K + k];
                            float w2 = W[((mm + 2) * Cg + c) * K + k], w3 = W[((mm + 3) * Cg + c) * K + k];
                            float *r0 = r[0], *r1 = r[1], *r2 = r[2], *r3 = r[3];
                            for (int64_t l = l0; l < l1; l++) {
                                float xv = xp[l];
                                r0[l] += w0 * xv; r1[l] += w1 * xv; r2[l] += w2 * xv; r3[l] += w3 * xv;
                            }
                        } else {
                            for (int64_t b = 0; b < blok; b++) {
                                float wv = W[((mm + b) * Cg + c) * K + k]; float *rb = r[b];
                                for (int64_t l = l0; l < l1; l++) rb[l] += wv * xp[l];
                            }
                        }
                    } else {
                        for (int64_t b = 0; b < blok; b++) {
                            float wv = W[((mm + b) * Cg + c) * K + k]; float *rb = r[b];
                            for (int64_t l = l0; l < l1; l++) rb[l] += wv * xr[l * st + ofs];
                        }
                    }
                }
            }
            mm += blok;
        }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_convt(oses_t *m, oses_dugum_t *dg, oses_tensor_t *x, oses_tensor_t *w, oses_tensor_t *bias, oses_tensor_t *o) {
    int64_t N = x->d[0], C = x->d[1], Lg = x->d[2], Mg = w->d[1], K = w->d[2];
    int64_t dil = dg->ia[0], grup = dg->ia[1], pl = dg->ia[3], pr = dg->ia[4], st = dg->ia[5], op_ = dg->ia[6];
    int64_t M = Mg * grup, Cg = C / grup;
    int64_t Lo = (Lg - 1) * st - pl - pr + dil * (K - 1) + op_ + 1;
    int64_t d[3] = { N, M, Lo };
    oses_tensor_t s = {0};
    if (yeni(m, &s, OD_F32, 3, d)) return -1;
    float *X = F(x), *W = F(w), *R = F(&s);
    for (int64_t n = 0; n < N; n++)
        for (int64_t mm = 0; mm < M; mm++) {
            float *r = R + (n * M + mm) * Lo;
            float bb = bias ? F(bias)[mm] : 0.0f;
            for (int64_t l = 0; l < Lo; l++) r[l] = bb;
            int64_t g = mm / Mg, mi = mm % Mg;
            for (int64_t c = 0; c < Cg; c++) {
                int64_t ci = g * Cg + c;
                const float *xr = X + (n * C + ci) * Lg;
                const float *wr = W + (ci * Mg + mi) * K;
                for (int64_t k = 0; k < K; k++) {
                    float wv = wr[k];
                    int64_t ofs = k * dil - pl;           /* l = i*st + ofs */
                    int64_t i0 = 0, i1 = Lg;
                    if (ofs < 0) i0 = (-ofs + st - 1) / st;
                    if (Lo - 1 - ofs < 0) continue;
                    int64_t imax = (Lo - 1 - ofs) / st + 1;
                    if (imax < i1) i1 = imax;
                    float *rp = r + ofs;
                    for (int64_t i = i0; i < i1; i++) rp[i * st] += xr[i] * wv;
                }
            }
        }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_matmul(oses_t *m, oses_tensor_t *a, oses_tensor_t *b, oses_tensor_t *o) {
    if (a->nd < 2 || b->nd < 2) HATA(m, "matmul 1B desteklenmiyor");
    int64_t Mx = a->d[a->nd - 2], K = a->d[a->nd - 1], K2 = b->d[b->nd - 2], Nx = b->d[b->nd - 1];
    if (K != K2) HATA(m, "matmul K");
    /* toplu boyutlar yayını */
    oses_tensor_t ab = *a, bb = *b;
    ab.nd = a->nd - 2; bb.nd = b->nd - 2;
    const oses_tensor_t *g[2] = { &ab, &bb };
    yayin_t y;
    if (ab.nd == 0 && bb.nd == 0) { y.nd = 0; y.n = 1; }
    else if (yayin_kur(m, &y, 2, g)) return -1;
    int64_t d[OSES_AZAMI_BOYUT];
    for (int j = 0; j < y.nd; j++) d[j] = y.d[j];
    d[y.nd] = Mx; d[y.nd + 1] = Nx;
    oses_tensor_t s = {0};
    if (yeni(m, &s, OD_F32, y.nd + 2, d)) return -1;
    float *A = F(a), *Bm = F(b), *R = F(&s);
    int64_t sa = Mx * K, sb = K * Nx, sr = Mx * Nx;
    YAYIN_DONGU(y, 2, {
        const float *pa = A + of[0] * sa, *pb = Bm + of[1] * sb;
        float *pr = R + _i * sr;
        for (int64_t i = 0; i < Mx; i++) {
            float *ri = pr + i * Nx;
            for (int64_t j = 0; j < Nx; j++) ri[j] = 0;
            for (int64_t k = 0; k < K; k++) {
                float av = pa[i * K + k];
                const float *bk = pb + k * Nx;
                for (int64_t j = 0; j < Nx; j++) ri[j] += av * bk[j];
            }
        }
    });
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_transpose(oses_t *m, oses_dugum_t *dg, oses_tensor_t *a, oses_tensor_t *o) {
    int nd = a->nd;
    int64_t perm[OSES_AZAMI_BOYUT];
    if (dg->ia[0] == 0) for (int i = 0; i < nd; i++) perm[i] = nd - 1 - i;
    else { if (dg->ia[0] != nd) HATA(m, "transpose perm"); for (int i = 0; i < nd; i++) perm[i] = dg->ia[1 + i]; }
    int64_t d[OSES_AZAMI_BOYUT], ad[OSES_AZAMI_BOYUT], kad[OSES_AZAMI_BOYUT];
    adimlar(a, ad);
    for (int i = 0; i < nd; i++) { d[i] = a->d[perm[i]]; kad[i] = ad[perm[i]]; }
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, nd, d)) return -1;
    int64_t say[OSES_AZAMI_BOYUT] = {0}, of = 0;
    for (int64_t i = 0; i < s.n; i++) {
        oge_kopya(s.b->v, i, a->b->v, of, a->dt);
        for (int j = nd - 1; j >= 0; j--) {
            say[j]++; of += kad[j];
            if (say[j] < d[j]) break;
            of -= kad[j] * d[j]; say[j] = 0;
        }
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_slice(oses_t *m, oses_dugum_t *dg, oses_tensor_t **g, oses_tensor_t *o) {
    oses_tensor_t *a = g[0], *bas = g[1], *son = g[2], *ek = dg->nin > 3 ? g[3] : 0, *adm = dg->nin > 4 ? g[4] : 0;
    int nd = a->nd;
    int64_t b[OSES_AZAMI_BOYUT], s_[OSES_AZAMI_BOYUT], st[OSES_AZAMI_BOYUT], d[OSES_AZAMI_BOYUT];
    for (int i = 0; i < nd; i++) { b[i] = 0; s_[i] = a->d[i]; st[i] = 1; }
    for (int64_t k = 0; k < bas->n; k++) {
        int ax = ek ? (int)eksen(tam(ek, k), nd) : (int)k;
        int64_t step = adm ? tam(adm, k) : 1;
        int64_t dim = a->d[ax], st0 = tam(bas, k), en = tam(son, k);
        if (st0 < 0) st0 += dim;
        if (en < 0) en += dim;
        if (step > 0) {
            if (st0 < 0) st0 = 0;
            if (st0 > dim) st0 = dim;
            if (en < 0) en = 0;
            if (en > dim) en = dim;
        } else {
            if (st0 < 0) st0 = -1;
            if (st0 > dim - 1) st0 = dim - 1;
            if (en < -1) en = -1;
            if (en > dim - 1) en = dim - 1;
        }
        b[ax] = st0; s_[ax] = en; st[ax] = step;
    }
    for (int i = 0; i < nd; i++) {
        int64_t n;
        if (st[i] > 0) n = s_[i] > b[i] ? (s_[i] - b[i] + st[i] - 1) / st[i] : 0;
        else n = b[i] > s_[i] ? (b[i] - s_[i] + (-st[i]) - 1) / (-st[i]) : 0;
        d[i] = n;
    }
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, nd, d)) return -1;
    int64_t ad[OSES_AZAMI_BOYUT], say[OSES_AZAMI_BOYUT] = {0};
    adimlar(a, ad);
    int64_t of = 0;
    for (int i = 0; i < nd; i++) of += b[i] * ad[i];
    for (int64_t i = 0; i < s.n; i++) {
        oge_kopya(s.b->v, i, a->b->v, of, a->dt);
        for (int j = nd - 1; j >= 0; j--) {
            say[j]++; of += st[j] * ad[j];
            if (say[j] < d[j]) break;
            of -= st[j] * ad[j] * d[j]; say[j] = 0;
        }
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_concat(oses_t *m, oses_dugum_t *dg, oses_tensor_t **g, oses_tensor_t *o) {
    int nd = g[0]->nd;
    int ax = (int)eksen(dg->ia[0], nd);
    int64_t d[OSES_AZAMI_BOYUT];
    for (int i = 0; i < nd; i++) d[i] = g[0]->d[i];
    d[ax] = 0;
    for (int k = 0; k < dg->nin; k++) d[ax] += g[k]->d[ax];
    oses_tensor_t s = {0};
    if (yeni(m, &s, g[0]->dt, nd, d)) return -1;
    int64_t dis = 1, ic = 1;
    for (int i = 0; i < ax; i++) dis *= d[i];
    for (int i = ax + 1; i < nd; i++) ic *= d[i];
    size_t eb = dt_boy(g[0]->dt);
    int64_t ofs = 0;
    for (int k = 0; k < dg->nin; k++) {
        int64_t parca = g[k]->d[ax] * ic;
        for (int64_t i = 0; i < dis; i++)
            bel_kopya((uint8_t *)s.b->v + (size_t)(i * d[ax] * ic + ofs) * eb,
                      (uint8_t *)g[k]->b->v + (size_t)(i * parca) * eb, (size_t)parca * eb);
        ofs += parca;
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_gather(oses_t *m, oses_dugum_t *dg, oses_tensor_t *a, oses_tensor_t *ind, oses_tensor_t *o) {
    int nd = a->nd, ax = (int)eksen(dg->ia[0], nd);
    if (ax < 0 || ax >= nd || nd - 1 + ind->nd > OSES_AZAMI_BOYUT) HATA(m, "gather boyut");
    int64_t d[OSES_AZAMI_BOYUT] = {0}; int k = 0;
    for (int i = 0; i < ax; i++) d[k++] = a->d[i];
    for (int i = 0; i < ind->nd; i++) d[k++] = ind->d[i];
    for (int i = ax + 1; i < nd; i++) d[k++] = a->d[i];
    if (k > OSES_AZAMI_BOYUT) HATA(m, "gather boyut");
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, k, d)) return -1;
    int64_t dis = 1, ic = 1;
    for (int i = 0; i < ax; i++) dis *= a->d[i];
    for (int i = ax + 1; i < nd; i++) ic *= a->d[i];
    size_t eb = dt_boy(a->dt);
    int64_t ni = ind->n, dim = a->d[ax];
    for (int64_t i = 0; i < dis; i++)
        for (int64_t j = 0; j < ni; j++) {
            int64_t x = tam(ind, j); if (x < 0) x += dim;
            if (x < 0 || x >= dim) { tensor_birak(m, &s); HATA(m, "gather indeks"); }
            bel_kopya((uint8_t *)s.b->v + (size_t)((i * ni + j) * ic) * eb,
                      (uint8_t *)a->b->v + (size_t)((i * dim + x) * ic) * eb, (size_t)ic * eb);
        }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_gather_el(oses_t *m, oses_dugum_t *dg, oses_tensor_t *a, oses_tensor_t *ind, oses_tensor_t *o) {
    int nd = a->nd, ax = (int)eksen(dg->ia[0], nd);
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, ind->nd, ind->d)) return -1;
    int64_t ad[OSES_AZAMI_BOYUT], say[OSES_AZAMI_BOYUT] = {0};
    adimlar(a, ad);
    for (int64_t i = 0; i < s.n; i++) {
        int64_t of = 0;
        for (int j = 0; j < nd; j++) {
            int64_t x = (j == ax) ? tam(ind, i) : say[j];
            if (j == ax && x < 0) x += a->d[ax];
            of += x * ad[j];
        }
        oge_kopya(s.b->v, i, a->b->v, of, a->dt);
        for (int j = nd - 1; j >= 0; j--) { if (++say[j] < ind->d[j]) break; say[j] = 0; }
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_gather_nd(oses_t *m, oses_tensor_t *a, oses_tensor_t *ind, oses_tensor_t *o) {
    int q = ind->nd; int64_t r = ind->d[q - 1];
    int64_t d[OSES_AZAMI_BOYUT]; int k = 0;
    for (int i = 0; i < q - 1; i++) d[k++] = ind->d[i];
    for (int i = (int)r; i < a->nd; i++) d[k++] = a->d[i];
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, k, d)) return -1;
    int64_t ad[OSES_AZAMI_BOYUT]; adimlar(a, ad);
    int64_t parca = 1; for (int i = (int)r; i < a->nd; i++) parca *= a->d[i];
    int64_t say = ind->n / r;
    size_t eb = dt_boy(a->dt);
    for (int64_t i = 0; i < say; i++) {
        int64_t of = 0;
        for (int64_t j = 0; j < r; j++) { int64_t x = tam(ind, i * r + j); if (x < 0) x += a->d[j]; of += x * ad[j]; }
        bel_kopya((uint8_t *)s.b->v + (size_t)(i * parca) * eb, (uint8_t *)a->b->v + (size_t)of * eb, (size_t)parca * eb);
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_scatter_nd(oses_t *m, oses_tensor_t *a, oses_tensor_t *ind, oses_tensor_t *up, oses_tensor_t *o) {
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, a->nd, a->d)) return -1;
    size_t eb = dt_boy(a->dt);
    bel_kopya(s.b->v, a->b->v, (size_t)a->n * eb);
    int64_t r = ind->d[ind->nd - 1];
    int64_t ad[OSES_AZAMI_BOYUT]; adimlar(a, ad);
    int64_t parca = 1; for (int i = (int)r; i < a->nd; i++) parca *= a->d[i];
    int64_t say = r ? ind->n / r : 0;
    for (int64_t i = 0; i < say; i++) {
        int64_t of = 0;
        for (int64_t j = 0; j < r; j++) { int64_t x = tam(ind, i * r + j); if (x < 0) x += a->d[j]; of += x * ad[j]; }
        bel_kopya((uint8_t *)s.b->v + (size_t)of * eb, (uint8_t *)up->b->v + (size_t)(i * parca) * eb, (size_t)parca * eb);
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_pad(oses_t *m, oses_tensor_t *a, oses_tensor_t *pads, oses_tensor_t *val, oses_tensor_t *o) {
    int nd = a->nd;
    int64_t bas[OSES_AZAMI_BOYUT], d[OSES_AZAMI_BOYUT];
    for (int i = 0; i < nd; i++) { bas[i] = tam(pads, i); d[i] = a->d[i] + bas[i] + tam(pads, i + nd); }
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, nd, d)) return -1;
    double v = val && val->n ? deger(val, 0) : 0.0;
    for (int64_t i = 0; i < s.n; i++) {
        if (a->dt == OD_F32) F(&s)[i] = (float)v; else if (a->dt == OD_I64) L(&s)[i] = (int64_t)v; else B(&s)[i] = (uint8_t)v;
    }
    int64_t ad[OSES_AZAMI_BOYUT], sad[OSES_AZAMI_BOYUT], say[OSES_AZAMI_BOYUT] = {0};
    adimlar(a, ad); adimlar(&s, sad);
    for (int64_t i = 0; i < a->n; i++) {
        int64_t of = 0; int icerde = 1;
        for (int j = 0; j < nd; j++) { int64_t x = say[j] + bas[j]; if (x < 0 || x >= d[j]) { icerde = 0; break; } of += x * sad[j]; }
        if (icerde) oge_kopya(s.b->v, of, a->b->v, i, a->dt);
        for (int j = nd - 1; j >= 0; j--) { if (++say[j] < a->d[j]) break; say[j] = 0; }
    }
    tensor_birak(m, o); *o = s;
    return 0;
}

static int op_azalt(oses_t *m, int op, int64_t *ax_l, int nax, int keep, oses_tensor_t *a, oses_tensor_t *o) {
    int nd = a->nd;
    uint8_t secili[OSES_AZAMI_BOYUT] = {0};
    if (nax == 0) for (int i = 0; i < nd; i++) secili[i] = 1;
    for (int i = 0; i < nax; i++) {
        int64_t e = eksen(ax_l[i], nd);
        if (e < 0 || e >= nd) HATA(m, "azaltma ekseni gecersiz");
        secili[e] = 1;
    }
    int64_t d[OSES_AZAMI_BOYUT] = {0}, dk[OSES_AZAMI_BOYUT]; int k = 0;
    for (int i = 0; i < nd; i++) {
        dk[i] = secili[i] ? 1 : a->d[i];
        if (!secili[i]) d[k++] = a->d[i]; else if (keep) d[k++] = 1;
    }
    oses_tensor_t s = {0};
    if (yeni(m, &s, a->dt, k, d)) return -1;
    /* çıktı (keep biçiminde) adımları */
    int64_t oad[OSES_AZAMI_BOYUT], st = 1, adet = 1;
    for (int i = nd - 1; i >= 0; i--) { oad[i] = secili[i] ? 0 : st; st *= dk[i]; }
    for (int i = 0; i < nd; i++) if (secili[i]) adet *= a->d[i];
    double *top = m->bel.ayir(sizeof(double) * (size_t)(s.n ? s.n : 1));
    if (!top) { tensor_birak(m, &s); HATA(m, "bellek yok"); }
    for (int64_t i = 0; i < s.n; i++) top[i] = (op == O_ReduceMax) ? -1e308 : 0;
    int64_t say[OSES_AZAMI_BOYUT] = {0}, of = 0;
    for (int64_t i = 0; i < a->n; i++) {
        double v = deger(a, i);
        if (op == O_ReduceMax) { if (v > top[of]) top[of] = v; } else top[of] += v;
        for (int j = nd - 1; j >= 0; j--) {
            say[j]++; of += oad[j];
            if (say[j] < a->d[j]) break;
            of -= oad[j] * a->d[j]; say[j] = 0;
        }
    }
    for (int64_t i = 0; i < s.n; i++) {
        double v = top[i];
        if (op == O_ReduceMean) v /= (double)adet;
        if (a->dt == OD_F32) F(&s)[i] = (float)v; else L(&s)[i] = (int64_t)v;
    }
    m->bel.birak(top);
    tensor_birak(m, o); *o = s;
    return 0;
}

/* RandomNormalLike: kendi üreteç (xorshift + Box-Muller) */
static double tekduze(oses_t *m) {
    uint64_t x = m->tohum;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    m->tohum = x;
    return ((x >> 11) + 0.5) * (1.0 / 9007199254740992.0);
}

/* ------------------------------------------------------------------ */
/* yükleme                                                              */
/* ------------------------------------------------------------------ */
static uint32_t oku32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t oku64(const uint8_t *p) { return (uint64_t)oku32(p) | ((uint64_t)oku32(p + 4) << 32); }

int oses_yukle(oses_t *m, const uint8_t *v, size_t boy, oses_bellek_t bel) {
    bel_sifir(m, sizeof *m);
    m->bel = bel;
    m->tohum = 0x9E3779B97F4A7C15ull;
    if (boy < 28 || v[0] != 'O' || v[1] != 'S' || v[2] != 'E' || v[3] != 'S') HATA(m, "OSES degil");
    if (oku32(v + 4) != 1) HATA(m, "surum");
    m->tsay = oku32(v + 8); m->dsay = oku32(v + 12); m->gsay = oku32(v + 16); m->csay = oku32(v + 20);
    uint64_t veri_ofs = oku32(v + 24);
    const uint8_t *p = v + 28;
    m->giris = (const int32_t *)p; p += 4 * m->gsay;
    m->cikis = (const int32_t *)p; p += 4 * m->csay;
    m->t = bel.ayir(sizeof(oses_tensor_t) * m->tsay);
    m->sabit = bel.ayir(sizeof(oses_tensor_t) * m->tsay);
    m->dugum = bel.ayir(sizeof(oses_dugum_t) * m->dsay);
    m->son_kullanim = bel.ayir(sizeof(int32_t) * m->tsay);
    if (!m->t || !m->sabit || !m->dugum || !m->son_kullanim) HATA(m, "bellek yok");
    bel_sifir(m->t, sizeof(oses_tensor_t) * m->tsay);
    bel_sifir(m->sabit, sizeof(oses_tensor_t) * m->tsay);
    for (uint32_t i = 0; i < m->tsay; i++, p += 48) {
        m->son_kullanim[i] = -1;
        if (p + 48 > v + boy) HATA(m, "tensor tablosu kesik");
        if (p[0] != 1) continue;
        oses_tensor_t *t = &m->sabit[i];
        t->dt = p[1]; t->nd = p[2]; t->n = 1;
        for (int j = 0; j < t->nd; j++) { t->d[j] = (int32_t)oku32(p + 8 + 4 * j); t->n *= t->d[j]; }
        uint64_t ofs = oku64(p + 32), bay = oku64(p + 40);
        if (veri_ofs + ofs + bay > boy || bay != (uint64_t)t->n * dt_boy(t->dt)) HATA(m, "sabit veri tasiyor");
        oses_tampon_t *b = bel.ayir(sizeof *b);
        if (!b) HATA(m, "bellek yok");
        b->v = (void *)(v + veri_ofs + ofs); b->sabit = 1; b->sayac = 1;   /* model kalıcı sahip */
        t->b = b;
    }
    for (uint32_t i = 0; i < m->dsay; i++) {
        oses_dugum_t *d = &m->dugum[i];
        if (p + 4 > v + boy) HATA(m, "dugum tablosu kesik");
        d->op = (uint16_t)(p[0] | (p[1] << 8)); d->nin = p[2]; d->nout = p[3]; p += 4;
        d->in = (const int32_t *)p; p += 4 * d->nin;
        d->out = (const int32_t *)p; p += 4 * d->nout;
        for (int j = 0; j < 16; j++) { d->ia[j] = (int64_t)oku64(p); p += 8; }
        for (int j = 0; j < 4; j++) { uint32_t u = oku32(p); bel_kopya(&d->fa[j], &u, 4); p += 4; }
        if (d->op >= O_SAYI) HATA(m, "bilinmeyen islem");
        for (int j = 0; j < d->nin; j++) if (d->in[j] >= 0) m->son_kullanim[d->in[j]] = (int32_t)i;
    }
    for (uint32_t i = 0; i < m->csay; i++) m->son_kullanim[m->cikis[i]] = (int32_t)m->dsay;
    return 0;
}

void oses_birak(oses_t *m) {
    if (m->t) for (uint32_t i = 0; i < m->tsay; i++) tensor_birak(m, &m->t[i]);
    if (m->sabit) for (uint32_t i = 0; i < m->tsay; i++) if (m->sabit[i].b) m->bel.birak(m->sabit[i].b);
    if (m->t) m->bel.birak(m->t);
    if (m->sabit) m->bel.birak(m->sabit);
    if (m->dugum) m->bel.birak(m->dugum);
    if (m->son_kullanim) m->bel.birak(m->son_kullanim);
}

/* ------------------------------------------------------------------ */
/* çalıştırma                                                           */
/* ------------------------------------------------------------------ */
static oses_tensor_t *tgir(oses_t *m, int32_t id) {
    if (id < 0) return 0;
    oses_tensor_t *t = &m->t[id];
    if (!t->b && m->sabit[id].b) {               /* sabit: kabuk paylaşılır */
        *t = m->sabit[id]; t->b->sayac++;
    }
    return t->b ? t : 0;
}

static int dugum_calistir(oses_t *m, int di) {
    oses_dugum_t *dg = &m->dugum[di];
    oses_tensor_t *g[8] = {0};
    for (int i = 0; i < dg->nin && i < 8; i++) g[i] = tgir(m, dg->in[i]);
    oses_tensor_t *o = &m->t[dg->out[0]];
    switch (dg->op) {
    case O_Mul: case O_Add: case O_Sub: case O_Div: case O_Pow:
    case O_Equal: case O_Less: case O_LessOrEqual: case O_GreaterOrEqual: case O_And:
        if (!g[0] || !g[1]) HATA(m, "ikili giris yok");
        return ikili(m, dg->op, g[0], g[1], o, devralinir(m, di, 0), devralinir(m, di, 1));
    case O_Sqrt: case O_Erf: case O_Tanh: case O_Sigmoid: case O_LeakyRelu: case O_Neg:
    case O_Exp: case O_Relu: case O_Softplus: case O_Ceil: case O_Not:
        if (!g[0]) HATA(m, "tekli giris yok");
        return tekli(m, dg->op, g[0], o, dg->fa[0], devralinir(m, di, 0));
    case O_Shape: {
        int64_t d[1] = { g[0]->nd };
        oses_tensor_t s = {0};
        if (yeni(m, &s, OD_I64, 1, d)) return -1;
        for (int i = 0; i < g[0]->nd; i++) L(&s)[i] = g[0]->d[i];
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_Unsqueeze: case O_Squeeze: {
        oses_tensor_t *a = g[0];
        int64_t d[OSES_AZAMI_BOYUT]; int nd;
        if (dg->op == O_Unsqueeze) {
            if (!g[1] || g[1]->n < 0 || a->nd + g[1]->n > OSES_AZAMI_BOYUT) HATA(m, "unsqueeze boyut");
            nd = a->nd + (int)g[1]->n;
            uint8_t ek[OSES_AZAMI_BOYUT] = {0};
            for (int64_t i = 0; i < g[1]->n; i++) {
                int64_t e = eksen(tam(g[1], i), nd);
                if (e < 0 || e >= nd || ek[e]) HATA(m, "unsqueeze ekseni gecersiz");
                ek[e] = 1;
            }
            int k = 0;
            for (int i = 0; i < nd; i++) d[i] = ek[i] ? 1 : a->d[k++];
        } else {
            uint8_t at[OSES_AZAMI_BOYUT] = {0};
            if (g[1]) for (int64_t i = 0; i < g[1]->n; i++) {
                int64_t e = eksen(tam(g[1], i), a->nd);
                if (e < 0 || e >= a->nd) HATA(m, "squeeze ekseni gecersiz");
                at[e] = 1;
            }
            else for (int i = 0; i < a->nd; i++) at[i] = a->d[i] == 1;
            nd = 0;
            for (int i = 0; i < a->nd; i++) if (!at[i]) d[nd++] = a->d[i];
        }
        return paylas(m, o, a, nd, d);
    }
    case O_Reshape: {
        oses_tensor_t *a = g[0], *sh = g[1];
        int64_t d[OSES_AZAMI_BOYUT]; int nd = (int)sh->n, bilinmeyen = -1; int64_t carp = 1;
        for (int i = 0; i < nd; i++) {
            d[i] = tam(sh, i);
            if (d[i] == 0 && !dg->ia[0]) d[i] = a->d[i];
            if (d[i] == -1) bilinmeyen = i; else carp *= d[i];
        }
        if (bilinmeyen >= 0) d[bilinmeyen] = carp ? a->n / carp : 0;
        return paylas(m, o, a, nd, d);
    }
    case O_Concat: return op_concat(m, dg, g, o);
    case O_Gather: return op_gather(m, dg, g[0], g[1], o);
    case O_GatherElements: return op_gather_el(m, dg, g[0], g[1], o);
    case O_GatherND: return op_gather_nd(m, g[0], g[1], o);
    case O_ScatterND: return op_scatter_nd(m, g[0], g[1], g[2], o);
    case O_Transpose: return op_transpose(m, dg, g[0], o);
    case O_Slice: return op_slice(m, dg, g, o);
    case O_Pad: return op_pad(m, g[0], g[1], dg->nin > 2 ? g[2] : 0, o);
    case O_ConstantOfShape: {
        int64_t d[OSES_AZAMI_BOYUT]; int nd = (int)g[0]->n;
        for (int i = 0; i < nd; i++) d[i] = tam(g[0], i);
        oses_tensor_t s = {0};
        if (yeni(m, &s, (int)dg->ia[0], nd, d)) return -1;
        for (int64_t i = 0; i < s.n; i++) {
            if (s.dt == OD_F32) F(&s)[i] = dg->fa[0]; else if (s.dt == OD_I64) L(&s)[i] = dg->ia[1]; else B(&s)[i] = (uint8_t)dg->ia[1];
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_Cast: {
        oses_tensor_t *a = g[0], s = {0};
        int dt = (int)dg->ia[0];
        if (yeni(m, &s, dt, a->nd, a->d)) return -1;
        for (int64_t i = 0; i < a->n; i++) {
            double v = deger(a, i);
            if (dt == OD_F32) F(&s)[i] = (a->dt == OD_I64) ? (float)L(a)[i] : (float)v;
            else if (dt == OD_I64) L(&s)[i] = (a->dt == OD_I64) ? L(a)[i] : (int64_t)v;
            else B(&s)[i] = v != 0;
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_Conv: return op_conv(m, dg, g[0], g[1], dg->nin > 2 ? g[2] : 0, o);
    case O_ConvTranspose: return op_convt(m, dg, g[0], g[1], dg->nin > 2 ? g[2] : 0, o);
    case O_MatMul: return op_matmul(m, g[0], g[1], o);
    case O_Expand: {
        oses_tensor_t *a = g[0], sh = {0};
        int64_t d[OSES_AZAMI_BOYUT]; int nd = (int)g[1]->n;
        for (int i = 0; i < nd; i++) d[i] = tam(g[1], i);
        sh.nd = (uint8_t)nd; for (int i = 0; i < nd; i++) sh.d[i] = d[i];
        const oses_tensor_t *gg[2] = { a, &sh };
        yayin_t y;
        if (yayin_kur(m, &y, 2, gg)) return -1;
        oses_tensor_t s = {0};
        if (yeni(m, &s, a->dt, y.nd, y.d)) return -1;
        YAYIN_DONGU(y, 1, oge_kopya(s.b->v, _i, a->b->v, of[0], a->dt));
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_Where: {
        const oses_tensor_t *gg[3] = { g[0], g[1], g[2] };
        yayin_t y;
        if (yayin_kur(m, &y, 3, gg)) return -1;
        oses_tensor_t s = {0};
        if (yeni(m, &s, g[1]->dt, y.nd, y.d)) return -1;
        YAYIN_DONGU(y, 3, { if (B(g[0])[of[0]]) oge_kopya(s.b->v, _i, g[1]->b->v, of[1], s.dt); else oge_kopya(s.b->v, _i, g[2]->b->v, of[2], s.dt); });
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_ReduceMean: return op_azalt(m, O_ReduceMean, &dg->ia[1], (int)dg->ia[0], (int)dg->ia[15], g[0], o);
    case O_ReduceSum: {
        int64_t ax[OSES_AZAMI_BOYUT]; int n = 0;
        if (dg->nin > 1 && g[1]) for (int64_t i = 0; i < g[1]->n; i++) ax[n++] = tam(g[1], i);
        return op_azalt(m, O_ReduceSum, ax, n, (int)dg->ia[15], g[0], o);
    }
    case O_ReduceMax: return op_azalt(m, O_ReduceMax, 0, 0, (int)dg->ia[15], g[0], o);
    case O_Range: {
        oses_tensor_t s = {0};
        double a = deger(g[0], 0), b = deger(g[1], 0), dd = deger(g[2], 0);
        double nn = osm_ceil((b - a) / dd); int64_t n = nn > 0 ? (int64_t)nn : 0;
        int64_t d[1] = { n };
        if (yeni(m, &s, g[0]->dt, 1, d)) return -1;
        for (int64_t i = 0; i < n; i++) {
            if (s.dt == OD_F32) F(&s)[i] = (float)(a + i * dd); else L(&s)[i] = L(g[0])[0] + i * L(g[2])[0];
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_NonZero: {
        oses_tensor_t *a = g[0], s = {0};
        int64_t say = 0;
        for (int64_t i = 0; i < a->n; i++) if (deger(a, i) != 0) say++;
        int64_t d[2] = { a->nd, say };
        if (yeni(m, &s, OD_I64, 2, d)) return -1;
        int64_t k = 0, idx[OSES_AZAMI_BOYUT] = {0};
        for (int64_t i = 0; i < a->n; i++) {
            if (deger(a, i) != 0) { for (int j = 0; j < a->nd; j++) L(&s)[j * say + k] = idx[j]; k++; }
            for (int j = a->nd - 1; j >= 0; j--) { if (++idx[j] < a->d[j]) break; idx[j] = 0; }
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_Softmax: {
        oses_tensor_t *a = g[0], s = {0};
        int ax = (int)eksen(dg->ia[0], a->nd);
        if (ax != a->nd - 1) HATA(m, "softmax son eksen degil");
        if (yeni(m, &s, OD_F32, a->nd, a->d)) return -1;
        int64_t K = a->d[ax], satir = a->n / K;
        for (int64_t r = 0; r < satir; r++) {
            const float *x = F(a) + r * K; float *y = F(&s) + r * K;
            float mx = x[0]; for (int64_t k = 1; k < K; k++) if (x[k] > mx) mx = x[k];
            double top = 0;
            for (int64_t k = 0; k < K; k++) { double e = osm_exp((double)x[k] - mx); y[k] = (float)e; top += e; }
            for (int64_t k = 0; k < K; k++) y[k] = (float)(y[k] / top);
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_Split: {
        oses_tensor_t *a = g[0];
        int nd = a->nd, ax = (int)eksen(dg->ia[0], nd);
        int64_t dis = 1, ic = 1, bas = 0;
        for (int i = 0; i < ax; i++) dis *= a->d[i];
        for (int i = ax + 1; i < nd; i++) ic *= a->d[i];
        size_t eb = dt_boy(a->dt);
        for (int k = 0; k < dg->nout; k++) {
            int64_t parca = (dg->nin > 1 && g[1]) ? tam(g[1], k) : a->d[ax] / dg->nout;
            int64_t d[OSES_AZAMI_BOYUT]; for (int i = 0; i < nd; i++) d[i] = a->d[i]; d[ax] = parca;
            oses_tensor_t s = {0};
            if (yeni(m, &s, a->dt, nd, d)) return -1;
            for (int64_t i = 0; i < dis; i++)
                bel_kopya((uint8_t *)s.b->v + (size_t)(i * parca * ic) * eb,
                          (uint8_t *)a->b->v + (size_t)((i * a->d[ax] + bas) * ic) * eb, (size_t)(parca * ic) * eb);
            bas += parca;
            oses_tensor_t *ok = &m->t[dg->out[k]];
            tensor_birak(m, ok); *ok = s;
        }
        return 0;
    }
    case O_CumSum: {
        oses_tensor_t *a = g[0], s = {0};
        int nd = a->nd, ax = (int)eksen(tam(g[1], 0), nd);
        if (yeni(m, &s, a->dt, nd, a->d)) return -1;
        int64_t dis = 1, ic = 1, K = a->d[ax];
        for (int i = 0; i < ax; i++) dis *= a->d[i];
        for (int i = ax + 1; i < nd; i++) ic *= a->d[i];
        int haric = (int)dg->ia[0], ters = (int)dg->ia[1];
        for (int64_t i = 0; i < dis; i++)
            for (int64_t j = 0; j < ic; j++) {
                double top = 0;
                for (int64_t kk = 0; kk < K; kk++) {
                    int64_t k = ters ? K - 1 - kk : kk;
                    int64_t of = (i * K + k) * ic + j;
                    double v = deger(a, of);
                    if (haric) { if (a->dt == OD_F32) F(&s)[of] = (float)top; else L(&s)[of] = (int64_t)top; top += v; }
                    else { top += v; if (a->dt == OD_F32) F(&s)[of] = (float)top; else L(&s)[of] = (int64_t)top; }
                }
            }
        /* f32 toplamı float sırayla (ORT gibi) */
        if (a->dt == OD_F32 && !haric && !ters) {
            for (int64_t i = 0; i < dis; i++)
                for (int64_t j = 0; j < ic; j++) { float t = 0; for (int64_t k = 0; k < K; k++) { int64_t of = (i * K + k) * ic + j; t += F(a)[of]; F(&s)[of] = t; } }
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_Clip: {
        oses_tensor_t *a = g[0], s = {0};
        if (yeni(m, &s, a->dt, a->nd, a->d)) return -1;
        double lo = (dg->nin > 1 && g[1]) ? deger(g[1], 0) : -1e308, hi = (dg->nin > 2 && g[2]) ? deger(g[2], 0) : 1e308;
        for (int64_t i = 0; i < a->n; i++) {
            double v = deger(a, i); if (v < lo) v = lo; if (v > hi) v = hi;
            if (a->dt == OD_F32) F(&s)[i] = (float)v; else L(&s)[i] = (int64_t)v;
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    case O_RandomNormalLike: {
        oses_tensor_t *a = g[0], s = {0};
        if (m->gurultu && m->gurultu(m, dg->out[0], o, m->baglam) == 0) return 0;   /* sınama: dışarıdan */
        if (yeni(m, &s, OD_F32, a->nd, a->d)) return -1;
        for (int64_t i = 0; i < s.n; i += 2) {
            double u1 = tekduze(m), u2 = tekduze(m);
            double r = osm_sqrt(-2.0 * osm_log(u1)), t = 6.283185307179586 * u2;
            /* cos/sin: t in [0,2pi): kendi seri */
            double c = 1, sn = 0, x = t - 3.141592653589793, x2 = x * x, p = 1; c = 0; sn = 0;
            /* cos(x+pi) = -cos x, sin(x+pi) = -sin x */
            { double tc = 1, ts = x; c = 0; sn = 0; for (int k = 0; k < 20; k++) { c += tc; sn += ts; tc *= -x2 / ((2*k+1)*(2*k+2)); ts *= -x2 / ((2*k+2)*(2*k+3)); } }
            (void)p;
            F(&s)[i] = (float)(dg->fa[0] + dg->fa[1] * r * -c);
            if (i + 1 < s.n) F(&s)[i + 1] = (float)(dg->fa[0] + dg->fa[1] * r * -sn);
        }
        tensor_birak(m, o); *o = s; return 0;
    }
    default: HATA(m, "islem uygulanmadi");
    }
}

long oses_calistir(oses_t *m, const int64_t *ids, int n, float og, float ou, float ow, float **ses) {
    m->hata[0] = 0;
    /* girişler: input [1,n], input_lengths [1], scales [3] */
    int64_t d2[2] = { 1, n }, d1[1] = { 1 }, d3[1] = { 3 };
    if (yeni(m, &m->t[m->giris[0]], OD_I64, 2, d2)) return -1;
    for (int i = 0; i < n; i++) L(&m->t[m->giris[0]])[i] = ids[i];
    if (yeni(m, &m->t[m->giris[1]], OD_I64, 1, d1)) return -1;
    L(&m->t[m->giris[1]])[0] = n;
    if (yeni(m, &m->t[m->giris[2]], OD_F32, 1, d3)) return -1;
    F(&m->t[m->giris[2]])[0] = og; F(&m->t[m->giris[2]])[1] = ou; F(&m->t[m->giris[2]])[2] = ow;
    for (uint32_t i = 0; i < m->dsay; i++) {
        if (dugum_calistir(m, (int)i) != 0) {
            if (!m->hata[0]) oses_hata(m, "dugum hatasi");
            return -1;
        }
        if (m->dugum[i].op == O_Ceil && m->bas_en_az > 0) {
            /* süreler [1,1,2n+1]: ^,0,ses1,0,... -> ilk gerçek ses 2. konumda */
            oses_tensor_t *w = &m->t[m->dugum[i].out[0]];
            if (w->dt == OD_F32 && w->n > 2 && F(w)[2] < (float)m->bas_en_az) {
                if (w->b->sayac == 1) F(w)[2] = (float)m->bas_en_az;
            }
        }
        if (m->izle) m->izle(m, (int)i, m->baglam);
        /* son kullanımı geçen girişleri bırak */
        oses_dugum_t *dg = &m->dugum[i];
        for (int j = 0; j < dg->nin; j++) {
            int32_t id = dg->in[j];
            if (id >= 0 && m->son_kullanim[id] == (int32_t)i) tensor_birak(m, &m->t[id]);
        }
    }
    oses_tensor_t *c = &m->t[m->cikis[0]];
    if (!c->b) HATA(m, "cikis yok");
    float *s = m->bel.ayir(sizeof(float) * (size_t)c->n);
    if (!s) HATA(m, "bellek yok (cikis)");
    bel_kopya(s, c->b->v, sizeof(float) * (size_t)c->n);
    long n_ = (long)c->n;
    tensor_birak(m, c);
    *ses = s;
    return n_;
}
